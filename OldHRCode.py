import time
import math
from ssd1306 import SSD1306_I2C
from machine import UART, Pin, I2C, ADC
from filefifo import Filefifo
from fifo import Fifo
from led import Led
from piotimer import Piotimer
import micropython
from kubios import Kubios
from mqtt_publish import Mqtt
#import extraction
import ujson
import os

#refer to function measure_hr() for heart rate measurement implementation




class Encoder: #move this to another file
    def __init__(self, rot_a = 10, rot_b = 11):
        self.a = Pin(rot_a, mode = Pin.IN, pull = Pin.PULL_UP)
        self.b = Pin(rot_b, mode = Pin.IN, pull = Pin.PULL_UP)
        self.fifo = Fifo(30, typecode = 'i')
        self.a.irq(handler = self.handler, trigger = Pin.IRQ_RISING, hard = True)
        
    def handler(self, pin):
        if self.b():
            self.fifo.put(-1)
        else:
            self.fifo.put(+1)
            

micropython.alloc_emergency_exception_buf(200)
samples = Fifo(2000)
press = Fifo(10)
sample_list = []
heartrates = [] 
peakcounts = []
history_list = []
text_pos_magn = [0, 2, 4, 6, 8, 10]
max_sample = 0
minhr = 30
maxhr = 240
pts = 0
ppis = []
gap_ms = 4
i2c = I2C(1, scl=Pin(15), sda=Pin(14), freq=400000)
kubios = Kubios()
mqtt = Mqtt("KME761_Group_2", "hw1Group2", "192.168.2.253")
oled_width = 128
oled_height = 64
character_width = 8
text_height = 8
oled = SSD1306_I2C(oled_width, oled_height, i2c)
sensor = ADC(Pin(26)) #ADC_0
rot = Encoder(10,11)
rot_butt = Pin(12, Pin.IN, pull = Pin.PULL_UP)

def button_handler(pin):
    global ts
    ts = time.ticks_ms()
    press.put(1)
    
rot_butt.irq(handler = button_handler, trigger = Pin.IRQ_FALLING, hard = True)


def start_menu():
    global pts, highlighted_text, max_sample
    highlighted_text = 0
    print("Into start_menu")
    oled.fill(0)
    oled.text("MEASURE HR", 0, 0 + 1, 1)
    oled.text("BASIC HRV ANALYSIS", 0, (text_height * text_pos_magn[1]) + 1, 1)
    oled.text("HISTORY", 0, (text_height * text_pos_magn[2]) + 1, 1)
    oled.text("KUBIOS", 0, (text_height * text_pos_magn[3]) + 1, 1)
    oled.show()

    while True:
        if highlighted_text != 0:
            pos = text_pos_magn[highlighted_text - 1]
            oled.rect(0, text_height * pos, oled_width, text_height + 2, 1)
        
        oled.show()
        if press.has_data():
            value = press.get()
            if ts - pts < 250:
                #print(ts - pts)
                pts = ts
                continue
            else:
                #print(ts - pts)
                pts = ts
                break
        
        while rot.fifo.has_data():
            value = rot.fifo.get()
            if value == 1:
                if highlighted_text + 1 < 5:
                    pos = text_pos_magn[highlighted_text - 1]
                    oled.rect(0, text_height * pos, oled_width, text_height + 2, 0)
                    highlighted_text += 1

            elif value == -1:
                if highlighted_text > 1:
                    pos = text_pos_magn[highlighted_text - 1]
                    oled.rect(0, text_height * pos, oled_width, text_height + 2, 0)
                    highlighted_text -= 1

            
"""        if time.ticks_diff(time.ticks_ms(), pts) >= 250:
            break  """


def receiving_data():
        oled.fill(0)
        oled.text("Receiving", 24, 25, 1)
        oled.text("Data...", 47, 35, 1)
        oled.show()
        
            
def collecting_data():
        oled.fill(0)
        oled.text("Collecting", 24, 25, 1)
        oled.text("Data...", 47, 35, 1)
        oled.show()
        
def sending_data():
        oled.fill(0)
        oled.text("Data", 24, 25, 1)
        oled.text("Sent!!!", 47, 35, 1)
        oled.show()
        time.sleep(2)
        
def error_data():
        oled.fill(0)
        oled.text(" Error sending", 6, 2, 1)
        oled.text("data.", 45, 12, 1)
        oled.text("Press button to", 5, 24, 1)
        oled.text("retry or wait", 8, 34, 1)
        oled.text("3s to return to", 2, 43, 1)
        oled.text("menu.", 45, 51, 1)
        oled.show()
        time.sleep(3)
def hrv_analysis(ppis, stay = True):
    global ts, pts
    ibi_diff = 0
    if len(ppis) == 0:
        oled.fill(0)
        oled.text("Measure HR first", 0, 30, 1)
        oled.show()
        time.sleep(3)
        return False
    mean_ppi = sum(ppis)/len(ppis)
    
    mean_HR = 60000/mean_ppi
    
    for ppi in ppis:
        ibi_diff += (ppi - mean_ppi)**2
    ssdn = math.sqrt(ibi_diff/len(ppis) - 1)
    
    for i in range(len(ppis) - 1):
        ibi_diff += (ppis[i] - ppis[i + 1])**2
    rmssd = math.sqrt(ibi_diff/len(ppis) - 1)
    
    measurement = {
    "mean_hr": round(mean_HR),
    "mean_ppi": round(mean_ppi),
    "rmssd": round(rmssd),
    "sdnn": round(ssdn)
    }
    if len(history_list) >= 5:
        history_list.remove(history_list[0])
    history_list.append(measurement)
    oled.fill(0)
    count = 0
    for term, measure in measurement.items():
        oled.text(f"{term}: {measure}", 0, (text_height * text_pos_magn[count]) - 3 * text_pos_magn[count], 1)
        count += 1
    oled.show()
    if stay == False:
        return measurement
    while True:
        if press.has_data():
            if ts - pts < 250:
                #print(ts - pts)
                pts = ts
                continue
            else:
                #print(ts - pts)
                pts = ts
                break
    json_message = measurement #.json()
    
    return json_message

            
def measure_hr():
    collecting_data()
    def get_signal(tid):
        samples.put(sensor.read_u16())

    timer = Piotimer(period = 4, mode = Piotimer.PERIODIC, callback = get_signal)
    
    global ppis, heartrates, sample_list, max_sample, peakcounts, pts, ts
    while True:
        if samples.has_data():
            sample = samples.get()
            sample_list.append(sample)
            
            if len(sample_list) >= 750:
                max_value = max(sample_list)
                min_value = min(sample_list)
                threshhold = (4*max_value + min_value)/5
                #print(max_value, threshhold)
                #gathering peak counts
                for i in sample_list:
                    if i >= threshhold and i > max_sample:
                        max_sample = i
                    elif i < threshhold and max_sample != 0:
                        try:
                           # print(max_sample)
                            index = sample_list.index(max_sample)
                            peakcounts.append(index)
                            max_sample = 0
                        except ValueError:
                            print("Please put the sensor back on your pulse and wait for recalibration(10 seconds)")
                            oled.fill(0)
                            oled.text("Pulse not detected", 0, 30, 1)
                            oled.show()
                            if press.has_data():
                                value = press.get()
                                if ts - pts < 250:
                                    #print(ts - pts)
                                    pts = ts
                                    continue
                                else:
                                   # print(ts - pts)
                                    pts = ts
                                    peakcounts = []
                                    sample_list = []
                                    timer.deinit()
                                    return
                       
                for i in range(len(peakcounts)):
                    delta = peakcounts[i] - peakcounts [i - 1]
                    ppi = delta * gap_ms
                    if press.has_data():
                        value = press.get()
                        if ts - pts < 250:
                            #print(ts - pts)
                            pts = ts
                            continue
                        else:
                           # print(ts - pts)
                            pts = ts
                            timer.deinit()
                            return

                    
                    if ppi > 300 and ppi < 1200:
                        heartrate = 60000/ppi
                        heartrate = round(heartrate)
                        print(f"HR : {heartrate} BPM")
                        if len(heartrates) > 1:
                            if heartrates[-1] - heartrates[-2] > 20 or heartrates[-2] - heartrates[-1] > 20:
                                oled.text("Noise detected", 0, 1)
                                oled.show()
                            
                        if heartrate > minhr and heartrate < maxhr:
                            oled.fill(0)
                            oled.rect(oled_width - (character_width * 4), oled_height - (text_height + 1), character_width * 4, text_height, 1, 1)
                            oled.text("STOP", oled_width - (character_width * 4), oled_height - text_height, 0)
                            #oled.rect(0, oled_height - text_height, character_width * 10, text_height, 0, 1)
                            oled.text(f"HR: {heartrate} BPM", 0, oled_height - text_height, 1)
                            oled.show()
                            ppis.append(ppi)
                            prev_hr = heartrate

                
                sample_list = []
                peakcounts = []

def history():
    global ts, pts
    oled.fill(0)
    if len(history_list) == 0:
        oled.fill(0)
        oled.text("Measure HR first", 0, 30, 1)
        oled.show()
        time.sleep(3)
        return
    for i in range(len(history_list)):
        oled.text(f"Measurement {i + 1}", 0, (text_height * text_pos_magn[i]) + 1, 1)
    highlighted_file = 0
    oled.show()
    while True:
        if highlighted_file != 0:
            pos = text_pos_magn[highlighted_file - 1]
            oled.rect(0, text_height * pos, oled_width, text_height + 2, 1)
        
        oled.show()
        
        while rot.fifo.has_data():
            value = rot.fifo.get()
            if value == 1:
                if highlighted_file + 1 < len(history_list) + 1:
                    pos = text_pos_magn[highlighted_file - 1]
                    oled.rect(0, text_height * pos, oled_width, text_height + 2, 0)
                    highlighted_file += 1

            elif value == -1:
                if highlighted_file > 1:
                    pos = text_pos_magn[highlighted_file - 1]
                    oled.rect(0, text_height * pos, oled_width, text_height + 2, 0)
                    highlighted_file -= 1
                    
        
        if press.has_data():
            value = press.get()
            if ts - pts < 250:
                #print(ts - pts)
                pts = ts
                continue
            else:
                #print(ts - pts)
                pts = ts
                displaytext = history_list[highlighted_file - 1]
                count = 0
                oled.fill(0)
                for terms, measures in displaytext.items():
                    oled.text(f" {terms}: {measures}", 0, (text_height * text_pos_magn[count]) - 3 * text_pos_magn[count], 1)
                    count += 1
                oled.show()
                while True:
                    if press.has_data():
                        if ts - pts < 250:
                            #print(ts - pts)
                            pts = ts
                            continue
                        else:
                            #print(ts - pts)
                            pts = ts
                            return



            
            

while True:
    if len(ppis) < 5:
        mqtt.connect_wlan()
        try:
            mqtt_client=mqtt.connect_mqtt()                
        except Exception as e:
            error_data()
            print(f"Failed to connect to MQTT: {e}")
        
        oled.fill(0)
        oled.text("Welcome to",24 ,25, 1)
        oled.text("PulsePal",32 ,35, 1)

        oled.text("Start", 44, 56, 1)
        oled.show()

        while not rot.fifo.has_data():
            if press.has_data(): #if user accidentally presses button before scroll
                data = press.get()
            pass

        data = rot.fifo.get()
        oled.rect(44,56,character_width * 5,text_height, 1, 1)
        oled.text("Start", 44, 56, 0)
        oled.show()


        while not press.has_data():
            pass

        data = press.get()
        pts = ts
    
    start_menu()
    
    


    if highlighted_text == 1:
        highlighted_text = 0
        measure_hr()


                    
    if highlighted_text == 2:
        highlighted_text = 0
        json_message =hrv_analysis(ppis)
        if json_message != False:
            json_message = ujson.dumps(json_message)
            #Connect to WLAN
            mqtt.connect_wlan()
            sending_data()
            
            # Connect to MQTT


            # Send MQTT message
            try:
                topic = "pulsepal/hrv"
                message = json_message
                mqtt_client.publish(topic, message)
                print(f"Sending to MQTT: {topic} -> {message}")
                    
            except Exception as e:
                print(f"Failed to send MQTT message: {e}")


    if highlighted_text == 3:
        highlighted_text = 0
        history()
    if highlighted_text == 4:
        highlighted_text = 0
        json_message = hrv_analysis(ppis, stay = False)
        print("Big up Kubios")
        if json_message != False:
            tup = kubios.connect(ppis)
            terms = ["pns","sns"]
            for i in range(len(tup)):
                print(terms[i], tup[i])
                oled.text(f"{terms[i]}: {tup[i]}", 0, (text_height * text_pos_magn[4 + i]) - 3 * text_pos_magn[4 + i], 1)
            oled.show()
            while True:
                if press.has_data():
                    if ts - pts < 250:
                        #print(ts - pts)
                        pts = ts
                        continue
                    else:
                        #print(ts - pts)
                        pts = ts
                        break
            