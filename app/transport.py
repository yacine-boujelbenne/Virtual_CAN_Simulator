"""Event-driven ISO-TP normal-addressing codec for CAN CC/CAN FD, PDUs <=4095 bytes.

The controller transports every emitted frame to a remote ECU. This module never
substitutes a whole UDS payload for its CAN frames. Reserved STmin and malformed
sequence/length errors are rejected. Extended addressing and >4095-byte PDUs are
deliberately outside this model.
"""
import time
FD_LENGTHS=(0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64)

def stmin_us(value):
    if 0<=value<=127:return value*1000
    if 0xf1<=value<=0xf9:return (value-0xf0)*100
    raise ValueError("Reserved ISO-TP STmin")

def validate_frame(frame):
    if not isinstance(frame,dict):raise ValueError("CAN frame must be an object")
    data=frame.get("data")
    if not isinstance(data,list) or any(type(b) is not int or not 0<=b<=255 for b in data):
        raise ValueError("CAN data must contain bytes 0..255")
    fd=bool(frame.get("fd",False))
    if len(data)>(64 if fd else 8) or (fd and len(data) not in FD_LENGTHS):
        raise ValueError("Invalid CAN/CAN FD wire length")
    identifier=frame.get("id")
    if type(identifier) is not int or not 0<=identifier<=(0x1fffffff if frame.get("extended") else 0x7ff):
        raise ValueError("Invalid CAN identifier")
    if frame.get("brs") and not fd:raise ValueError("BRS requires CAN FD")
    return frame

class IsoTp:
    def __init__(self,tx_id,rx_id,fd=False,link_length=8,block_size=8,stmin=0):
        if link_length not in (8,12,16,20,24,32,48,64) or (not fd and link_length!=8):
            raise ValueError("Invalid ISO-TP link length")
        if not 0<=block_size<=255:raise ValueError("Invalid block size")
        stmin_us(stmin)
        self.tx_id,self.rx_id,self.fd,self.link_length=tx_id,rx_id,fd,link_length
        self.bs,self.stmin=block_size,stmin
        self.tx=None;self.rx=None;self.message=None
    def frame(self,data,delay=0):
        if self.fd:
            length=next(n for n in FD_LENGTHS if n>=len(data));data=data+[0]*(length-len(data))
        frame=dict(id=self.tx_id,data=data,extended=self.tx_id>0x7ff,fd=self.fd,brs=self.fd,bus="diagnostics",delay_us=delay)
        validate_frame(frame);return frame
    def send(self,payload):
        if not payload or len(payload)>4095:raise ValueError("ISO-TP PDU must be 1..4095 bytes")
        if self.tx is not None:raise ValueError("ISO-TP transmitter busy")
        limit=self.link_length-2 if self.fd and self.link_length>8 else 7
        if len(payload)<=limit:
            header=[0,len(payload)] if self.fd and self.link_length>8 and len(payload)>7 else [len(payload)]
            return [self.frame(header+list(payload))]
        self.tx=dict(payload=list(payload),offset=self.link_length-2,sequence=1,waits=0)
        return [self.frame([0x10|(len(payload)>>8),len(payload)&255]+list(payload[:self.link_length-2]))]
    def receive(self,frame):
        validate_frame(frame)
        if frame["id"]!=self.rx_id:return []
        if frame.get("fd",False)!=self.fd:raise ValueError("ISO-TP link protocol mismatch")
        data=frame["data"]
        if not data:raise ValueError("Empty ISO-TP frame")
        kind=data[0]>>4
        if kind==0:
            length=data[0]&15;header=1
            if length==0 and self.fd and len(data)>8:length=data[1];header=2
            if not length or (header==1 and length>7) or len(data)<header+length:
                raise ValueError("Malformed ISO-TP single frame")
            self.message=data[header:header+length];return []
        if kind==1:
            if len(data)<8:raise ValueError("Malformed ISO-TP first frame")
            length=((data[0]&15)<<8)|data[1]
            if length<=len(data)-2:raise ValueError("Invalid first-frame length")
            self.rx=dict(length=length,payload=data[2:],sequence=1,count=0)
            return [self.frame([0x30,self.bs,self.stmin])]
        if kind==2:
            if self.rx is None or len(data)<2 or (data[0]&15)!=self.rx["sequence"]:
                raise ValueError("ISO-TP sequence error")
            rx=self.rx
            rx["payload"].extend(data[1:1+min(len(data)-1,rx["length"]-len(rx["payload"]))])
            rx["sequence"]=(rx["sequence"]+1)&15;rx["count"]+=1
            if len(rx["payload"])==rx["length"]:self.message=rx["payload"];self.rx=None;return []
            if self.bs and rx["count"]==self.bs:rx["count"]=0;return [self.frame([0x30,self.bs,self.stmin])]
            return []
        if kind==3:
            if self.tx is None or len(data)<3:raise ValueError("Unexpected/malformed flow control")
            status=data[0]&15
            if status==1:
                self.tx["waits"]+=1
                if self.tx["waits"]>3:raise ValueError("ISO-TP wait-frame limit exceeded")
                return []
            if status!=0:self.tx=None;raise ValueError("ISO-TP flow control aborted")
            delay=stmin_us(data[2]);block=data[1] or 4095;frames=[]
            tx=self.tx
            for _ in range(block):
                if tx["offset"]>=len(tx["payload"]):break
                count=min(self.link_length-1,len(tx["payload"])-tx["offset"])
                frames.append(self.frame([0x20|tx["sequence"]]+tx["payload"][tx["offset"]:tx["offset"]+count],delay))
                tx["offset"]+=count;tx["sequence"]=(tx["sequence"]+1)&15
            if tx["offset"]==len(tx["payload"]):self.tx=None
            return frames
        raise ValueError("Unsupported ISO-TP frame type")
