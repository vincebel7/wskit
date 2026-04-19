# Read values off GPIO pins
# I still have a fuzzy understanding of this, this was a lot of trial + error
# Special thanks to the many others who have provided similar libraries and 
# step-by-step tutorials on parsing signals off the GPIO pins.

import time
from pyA20.gpio import gpio, port

class DHTResponse:
        temperature = -1
        humidity = -1
        error_code = -1

        def __init__(self, temperature, humidity, error_code):
            self.temperature = temperature
            self.humidity = humidity
            self.error_code = error_code

        def __get_error_code(self):
            return self.error_code

        def get_error_text(code):
            if code == 0:
                return "Success"
            if code == 1:
                return "No data"
            if code == 2:
                return "Bad data"
            if code == 3:
                return "Invalid checksum"

class DHTSensor:
    __pin = -1

    def __init__(self, pin, sensor="DHT22"):
        self.__pin = pin

        if sensor in ["DHT11", "DHT22"]:
            self.__sensor = sensor
        else:
            raise ValueError("Invalid sensor model")

    def read_pin(self):

        # switch to putput for high/low
        gpio.setcfg(self.__pin, gpio.OUTPUT)

        gpio.output(self.__pin, 1)
        time.sleep(0.05) # 50 msec

        gpio.output(self.__pin, 0)
        time.sleep(0.02) # 20 msec

        # switch to input for listening
        gpio.setcfg(self.__pin, gpio.INPUT)
        gpio.pullup(self.__pin, gpio.PULLUP)

        raw_input = self.__input_listen()
        
        # Get period lengths
        # 1-8: Humidity (integer)
        # 9-16: Humidity (decimal)
        # 17-24: Temperature (integer)
        # 25-32: Temperature (decimal)
        # 33-40: Checksum
        period_lengths = self.__get_period_lengths(raw_input)
        
        # Period length validation
        if len(period_lengths) == 0:
            return DHTResponse(0, 0, 1)

        elif len(period_lengths) != 40:
            return DHTResponse(0, 0, 2)

        bits = self.__data_to_bits(period_lengths)
        mybytes = self.__bits_to_bytes(bits)
        checksum = self.__checksum(mybytes)

        if self.__sensor == "DHT22":
            mybytes = self.__dht22_compute(mybytes)

        if mybytes[4] != checksum:
            return DHTResponse(0, 0, 3)

        #return DHTResponse(self.temp_c_to_f(mybytes[2]), mybytes[0], 0)
        return DHTResponse(mybytes[2], mybytes[0], 0)

    def __input_listen(self):
        # input
        prev = -1
        repeat_counter = 0
        data = []
        bits_to_read = True

        while bits_to_read:
            input = gpio.input(self.__pin)
            data.append(input)

            if input == prev:
                repeat_counter += 1
                if repeat_counter > 50:
                    bits_to_read = False
            else:
                repeat_counter = 0

            prev = input

        return data

    def __get_period_lengths(self, data):
        periods = []
        payload_len = 0
        step = 1 # init down, init up, data down, data up, data down

        for i in range(len(data)):
            val = data[i]
            payload_len += 1

            if step == 1 and val == 0: # init low
                step = 2

            if step == 2 and val == 1: # init high
                step = 3

            if step == 3 and val == 0: # data down
                step = 4

            if step == 4 and val == 1: # data up
                payload_len = 0
                step = 5

            if step == 5 and val == 0: # data down, we are receiving payload
                periods.append(payload_len)
                step = 4

        return periods

    def __data_to_bits(self, data):
        bits = []
        shortest = 1000
        longest = 0

        for i in range(0, len(data)):
            length = data[i]
            if length < shortest:
                shortest = length
            if length > longest:
                longest = length

        halfway = shortest + (longest - shortest) / 2
        bits = []

        for i in range(0, len(data)):
            bit = False
            if data[i] > halfway:
                bit = True
            bits.append(bit)

        return bits

    def __bits_to_bytes(self, bits):
        mybytes = []
        byte = 0

        for i in range(0, len(bits)):
            byte = byte << 1
            if(bits[i]):
                byte = byte | 1
            else:
                byte = byte | 0
            
            if((i+1) % 8 == 0):
                mybytes.append(byte)
                byte = 0

        return mybytes

    def __checksum(self, mybytes):
        return mybytes[0] + mybytes[1] + mybytes[2] + mybytes[3] & 255

    def __dht22_compute(self, mybytes):
        c = (float)(((mybytes[2] & 0x7F) << 8) + mybytes[3]) / 10

        if c > 80:
            c = 80

        if mybytes[2] & 0x80:
            c = -c

        dht22_bytes = mybytes

        dht22_bytes[2] = c
        dht22_bytes[0] = ((mybytes[0] << 8) + mybytes[1]) / 10

        return dht22_bytes

    def temp_c_to_f(self, celsius):
        return((celsius * 1.8) + 32)
