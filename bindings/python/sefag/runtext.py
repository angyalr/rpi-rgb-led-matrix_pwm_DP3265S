#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Display a runtext with double-buffering.
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[3]
PYTHON_BINDINGS_DIR = PROJECT_ROOT / "bindings" / "python"
MS_APP_DIR = PYTHON_BINDINGS_DIR / "ms"
FONT_DIR = PROJECT_ROOT / "fonts"
sys.path.insert(0, str(MS_APP_DIR))

from samplebase import SampleBase
from rgbmatrix import graphics
import time
import datetime
import socketserver
import threading
import os
import RPi.GPIO as GPIO

from config_store import load_config, save_config

PROTOCOL_ENCODING = "cp1250"


def protocol_bytes(value):
    if isinstance(value, bytes):
        return value
    return str(value).encode(PROTOCOL_ENCODING)


def protocol_text(value):
    if isinstance(value, bytes):
        return value.decode(PROTOCOL_ENCODING, errors="replace")
    return str(value)

running= True
restart= False

messageTimeOut = int( 3)
restartDay = int( 1)
restartHour = int( 1)
restartMin = int( 1)

path= str(Path(__file__).resolve().parent) + "/"

Black = 0
Red   = 1
Green = 2
Blue  = 3
Purple= 4
Cian  = 5
Yellow= 6
White = 7

MonoChrome= int( 0)

ColorNames=['Fekete', 'Piros', 'Zöld', 'Kék', 'Lila', 'Ciánkék', 'Sárga', 'Fehér']
EffectNames= ['Balra', 'Jobbra', 'Középre', 'Fut', 'Villog']
FontNames= [ '4x6', '5x7', '5x8', '6x9', '6x10', #0..4 
            '6x12', '6x13', '6x13B', '6x13O', '7x13',  #5..9
            '7x13B', '7x13O', '7x14', '7x14B', '8x13',  # 10..14
            '8x13B', '8x13O', '9x15', '9x15B', '9x18',  #  15..19
            '9x18B', '10x20', 'helvB24', 'courB24', 'timB24',  #20..24 
            'helvR12', 'courB18', 'courR18', 'lubB18', 'lubB19',
            'arial20', 'arial18', 'arial17', 'arial16']  #30.. 
font1 = graphics.Font()
font2 = graphics.Font()
FontName= ''


def load_display_font(font, selected_index, fallback_name):
    requested_name = FontNames[selected_index]
    requested_path = FONT_DIR / (requested_name + ".bdf")
    fallback_path = FONT_DIR / (fallback_name + ".bdf")
    font_path = requested_path if requested_path.exists() else fallback_path
    if font_path != requested_path:
        print("Font not found:", requested_path, "- using", font_path)
    font.LoadFont(str(font_path))

GPIO.setmode(GPIO.BCM)
GPIO.setup(16, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(20, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(21, GPIO.IN, pull_up_down=GPIO.PUD_UP)

gpio16= int(0)
gpio20= int(0)
gpio21= int(0)
nullazni_kell= int( 0)
msgId= int( 0)
lastMsgTime= datetime.datetime.now()

def SaveConfig():
    try:
        Config= load_config()
        
        if not Config.has_section('restart'):
            Config.add_section('restart')
        Config.set('restart', 'day(1-7)', str(1))
        Config.set('restart', 'hour(0-23)', str(0))
        Config.set('restart', 'min(0-59)', str(0))

        if not Config.has_section('timeOut'):
            Config.add_section('timeOut')
        Config.set('timeOut', 'message(sec.)', str(3))

        save_config(Config)
    except Exception as error:
        print('Config mentés hiba.')
        print(error)  

def LoadConfig():
    global restartDay
    global restartHour
    global restartMin
    global messageTimeOut

    print('Load config')
    try:
        Config= load_config()
        if not Config.has_section('restart') or not Config.has_section('timeOut'):
            SaveConfig()
            Config= load_config()
        restartDay = int( Config.get('restart', 'day(1-7)', fallback=str(restartDay)))
        restartHour = int( Config.get('restart', 'hour(0-23)', fallback=str(restartHour)))
        restartMin = int( Config.get('restart', 'min(0-59)', fallback=str(restartMin)))
        messageTimeOut = int( Config.get('timeOut', 'message(sec.)', fallback=str(messageTimeOut)))

        print('Config file read OK')
    except Exception as error:
        SaveConfig()
        print('Config betöltés hiba. ', error)

LoadConfig()

def RGB( acolor):
    if acolor==Red:
        R=255; G=0; B=0;
    elif acolor==Green:
        R=0; G=255; B=0;
    elif acolor==Blue:
        R=0; G=0; B=127;
    elif acolor==Purple:
        R=255; G=0; B=255;
    elif acolor==Cian:
        R=0; G=255; B=255;
    elif acolor==Yellow:
        R=255; G=255; B=0;
    elif acolor==White:
        R=255; G=255; B=255;
    elif acolor==Black:
        R=0; G=0; B=0;
    else:
        R=255; G=255; B=255;
    return graphics.Color( R,G,B)


RunSpeed= int(20)
Brightness= int(5)
Effect1= int(3)
Effect2= int(3)
SelectFont1= int(10) 
SelectFont2= int(10) 
Color1= int(4)
Color2= int(5)
BackGround1= int(0)
BackGround2= int(0)
YPos1= int(7)
YPos2= int(31)
Msg1= str('')
Msg2= str('')
rcv_data= str('')

blinkPhase= 0
len1= 64
len2= 64
pos1= 0
pos2= 0

def load_rcv_init():
  global MonoChrome
  global rcv_data
  global new_data
  if MonoChrome==1:
    rcv_data= '\x027\x145331111007\x0f** Welcome! **\x0A** Hi Welcome! **\x03'
  else:
    rcv_data= '\x027\x145339\x1611339\x20** Welcome! **\x0A** Hi Welcome! **\x03'
  new_data= True

def  get_par( par):
    x= par
    if x>=ord( '0'):
        x= x- ord( '0')
    return x

def parse_data():
    msg= 'OK'
    global RunSpeed
    global Brightness
    global Effect1
    global Effect2
    global SelectFont1
    global SelectFont2
    global Color1
    global Color2
    global BackGround1
    global BackGround2
    global Msg1
    global Msg2
    global YPos1
    global YPos2
    global new_data
    global rcv_data
    
    try:
        if len( rcv_data)>13 and rcv_data[0]== chr(2):
            cmd= get_par( ord( rcv_data[ 1]))
            if cmd== 7:
                # speed
                par= get_par( ord(rcv_data[2]))
                RunSpeed= par
                par= get_par( ord(rcv_data[3]))
                if par>=1 and par<=10:
                    Brightness= par
                par= get_par( ord(rcv_data[4]))
                if par>=0 and par<=4:
                    Effect1= par
                par= get_par( ord(rcv_data[5]))
                if par>=0 and par<=4:
                    Effect2= par
                par= get_par( ord(rcv_data[6]))
                if par>=0 and par<=33:
                    SelectFont1= par
                    load_display_font(font1, SelectFont1, "7x13B")
                par= get_par( ord(rcv_data[7]))
                if par>=0 and par<=33:
                    SelectFont2= par
                    load_display_font(font2, SelectFont2, "7x13B")
                par= get_par( ord(rcv_data[8]))
                if par>=0 and par<=7:
                    Color1= par
                par= get_par( ord(rcv_data[9]))
                if par>=0 and par<=7:
                    Color2= par
                par= get_par( ord(rcv_data[10]))
                if par>=0 and par<=7:
                    BackGround1= par
                par= get_par( ord(rcv_data[11]))
                if par>=0 and par<=7:
                    BackGround2= par
                par= get_par( ord(rcv_data[12]))
                if par>=0 and par<=31:
                    YPos1= par
                par= get_par( ord(rcv_data[13]))
                if par>=0 and par<=31:
                    YPos2= par
                Msg1= rcv_data[13:]
                par= Msg1.find( chr(10))
                if par==-1:
                    Msg1= Msg1[:-1]
                    Msg2=''
                else:
                    Msg2= Msg1[(par+1):]
                    Msg2= Msg2[:-1]
                    Msg1= Msg1[:(par)]
                    Msg1= Msg1[1:]
                    par= Msg2.find( chr(3))
                    if par>=0:
                        Msg2= Msg2[(par+ 1):] 
                msg= Msg1+ chr(13)+ chr(10)+ Msg2+ chr(13)+ chr(10)
                if MonoChrome==1:
                    if Color1!= Red:
                        BackGround1= Red
                        Color1= Black
                    else:
                        Color1= Red
                        BackGround1= Black
                    if Color2!= Red:
                        BackGround2= Red
                        Color2= Black
                    else:
                        Color2= Red
                        BackGround2= Black
                    if YPos1>15:
                        YPos1= 15
                    if YPos2>15:
                        YPos2= 15
            else:
                msg= 'Nem értelmezhető a parancs!'
        else:
            msg= 'Nem megfelelő a formátum!'
    except:
        msg= 'parse hiba!'
    rcv_data= ''
    new_data= False
    return msg


class RunText(SampleBase):
    def __init__(self, *args, **kwargs):
        super(RunText, self).__init__(*args, **kwargs)
        self.parser.add_argument("-t", "--text", help="The text to scroll on the RGB LED panel",default="Hello world!")
        

    def run(self):
        global pos1
        global pos2
        global len1
        global len2
        global NewMsg
        global blinkPhase
        global Msg1
        global Msg2
        global restart
        global nullazni_kell
        global lastMsgTime
        global rcv_data
        global new_data
        global MonoChrome
        global messageTimeOut
        global restartDay
        global restartHour
        global restartMin
        global gpio16
        if self.args.led_multiplexing==18:
            MonoChrome= 1
        else:
            MonoChrome= 0
        print('multiplexing', self.args.led_multiplexing)
        print('MonoChrome:', MonoChrome)
        
        load_rcv_init()
        offscreen_canvas = self.matrix.CreateFrameCanvas()
        textColor1 = RGB(Purple)
        textColor2 = RGB(Cian)
        bkColor1= RGB( Black)
        bkColor2= RGB( Black)
        pos1 = offscreen_canvas.width
        pos2 = offscreen_canvas.width
        halfwidth = offscreen_canvas.width//2
        print('** Start **')
        
        while running and (restart== False):
            # vas�rnap 4-kor �jra indul
            t= datetime.datetime.now()
            w= datetime.datetime.today()
            if (t.hour== restartHour) and (t.minute== restartMin) and (t.second== 0) and (w.isoweekday()== restartDay):
                restart= True
            if new_data== True:
                parse_data()
                print(Msg1, '  ', YPos1, '  ', SelectFont1, '  ', Color1)
                print(Msg2, '  ', YPos2, '  ', SelectFont2, '  ', Color2)
                textColor1 = RGB( Color1)
                textColor2 = RGB( Color2)
                bkColor1= RGB( BackGround1)
                bkColor2= RGB( BackGround2)
                len1= graphics.DrawText(offscreen_canvas, font1, pos1, YPos1, textColor1, Msg1)
                len2= graphics.DrawText(offscreen_canvas, font2, pos2, YPos2, textColor2, Msg2)
            if BackGround1== Black:
              offscreen_canvas.Fill( 0, 0, 0)
            elif BackGround1== Red:
              offscreen_canvas.Fill( 255, 0, 0)
            elif BackGround1== Green:
              offscreen_canvas.Fill( 0, 255, 0)
            elif BackGround1== Blue:
              offscreen_canvas.Fill( 0, 0, 255)
            elif BackGround1== Purple:
              offscreen_canvas.Fill( 255, 0, 255)
            elif BackGround1== Cian:
              offscreen_canvas.Fill( 0, 255, 255)
            elif BackGround1== Yellow:
              offscreen_canvas.Fill( 255, 255, 0)
            elif BackGround1== White:
              offscreen_canvas.Fill( 255, 255, 255)
            if Effect1== 0:  # balra
                pos1= 0
            elif Effect1== 1:  # jobbra
                pos1= (offscreen_canvas.width- len1)
            elif Effect1== 2:  # k�z�pre
                pos1=  (offscreen_canvas.width- len1)//2
            elif Effect1== 3:  # fut
                pos1 -= 1
                if (pos1 + len1 < 0):
                    pos1 = halfwidth-1
            elif Effect1== 4:  # villog
                pos1= (offscreen_canvas.width- len1)//2
                blinkPhase= blinkPhase+1
                if blinkPhase>10:
                    blinkPhase= 0
            else:
                print('Effect error: ', str( Effect1))
            
            if (Effect1==4) or (Effect2==4):
                self.matrix.brightness= Brightness* blinkPhase
            else:
                self.matrix.brightness= Brightness* 10
            
            if Effect2== 0:  # balra
                pos2= 0
            elif Effect2== 1:  # jobbra
                pos2=  (offscreen_canvas.width- len2)
            elif Effect2== 2:  # k�z�pre
                pos2=  (offscreen_canvas.width- len2)//2
            elif Effect2== 3:  # fut
                pos2 -= 1
                if (pos2 + len2 < 0):
                    pos2 = halfwidth-1
            elif Effect2== 4:  # villog
                pos2= (offscreen_canvas.width- len2)//2
            else:
                print('Effect error: ', str( Effect2))
            
            graphics.DrawText(offscreen_canvas, font1, pos1, YPos1, textColor1, Msg1)
            if (Effect1==3) and ((pos1+ len1)< halfwidth):
                graphics.DrawText(offscreen_canvas, font1, pos1+ len1+ halfwidth, YPos1, textColor1, Msg1)
            graphics.DrawText(offscreen_canvas, font2, pos2+1, YPos2, textColor2, Msg2)
            if (Effect2==3) and ((pos2+ len2)< halfwidth):
                graphics.DrawText(offscreen_canvas, font2, pos2+ len2+ halfwidth, YPos2, textColor2, Msg2)
            
            speedTime= 0.010
            if (Effect1 in [3, 4]) or (Effect2 in [3, 4]):
                speedTime= float(RunSpeed)
                speedTime= speedTime/1000

            time.sleep(speedTime)
            offscreen_canvas = self.matrix.SwapOnVSync(offscreen_canvas)
            
            if GPIO.input( 16) == GPIO.LOW:
                gpio16+= 1
                if gpio16==4:
                    nullazni_kell = 1
            else:
                gpio16= 0
                
            try:
                n= datetime.datetime.now()
                i= (n- lastMsgTime).seconds
                if i> messageTimeOut:
                    lastMsgTime= datetime.datetime.now()
                    if MonoChrome==1:
                        rcv_data= '\x027\x145331111007\x0f** nincs kapcsolat **\x0A** Error! **\x03'
                    else: 
                        rcv_data= '\x027\x145339\x1711339\x20** nincs kapcsolat **\x0A** Error! **\x03'
                    new_data= True
            except Exception as error:
                print('timeout exception ', error)
# end of run            


class ThreadedTCPRequestHandler(socketserver.BaseRequestHandler):
    def handle(self):
        global new_data
        global rcv_data
        global running
        global data
        global msgId
        global nullazni_kell
        global lastMsgTime
        while running:
            try:
                data = protocol_text(self.request.recv(1024))
                if len( data)>0:
                    if (data[0]== chr(2)) and (data[1]== '7'):
                        rcv_data= data
                        new_data= True
                        msgId= msgId+ 1;
                        self.request.sendall(protocol_bytes('OK , ' + str(msgId)))
                        if msgId == 1000:
                            msgId= 0
                        lastMsgTime= datetime.datetime.now() 
                    else:
                        self.request.sendall(protocol_bytes('Error'))
                if nullazni_kell:
                    self.request.sendall(protocol_bytes('Button1'))
                    nullazni_kell= 0
            except Exception as error:
                print('server data exception ', error)
                

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    pass

# Main function
if __name__ == "__main__":

    run_text = RunText()
    
    # Port 0 means to select an arbitrary unused port
    HOST, PORT = "", 9999
    server = ThreadedTCPServer((HOST, PORT), ThreadedTCPRequestHandler)
    ip, port = server.server_address
    # Start a thread with the server -- that thread will then start one
    # more thread for each request
    server_thread = threading.Thread(target=server.serve_forever)
    # Exit the server thread when the main thread terminates
    server_thread.daemon = True
    server_thread.start()
    print("TCP Server running...", str(ip), ':', str(port))
    
    try:
        run_text.process()
    finally:
        running= False
        print("server shutdown...")
        server.shutdown()
        server.server_close()
        print("sys.exit...")
        if restart== True:
            os.system('sudo shutdown -r now')
        sys.exit(0)
