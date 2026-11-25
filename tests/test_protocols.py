import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/"app"))
from transport import IsoTp,validate_frame,stmin_us

class Protocols(unittest.TestCase):
    def transfer(self,size,fd=False,length=8,block=8):
        a=IsoTp(0x700,0x708,fd,length,block,0)
        b=IsoTp(0x708,0x700,fd,length,block,0)
        payload=[i%256 for i in range(size)];queue=[(b,f) for f in a.send(payload)];trace=[]
        while queue:
            target,frame=queue.pop(0);trace.append(frame)
            reply=target.receive(frame);other=a if target is b else b
            queue.extend((other,f) for f in reply)
        self.assertEqual(b.message,payload)
        self.assertTrue(all(len(f["data"])<=(64 if fd else 8) for f in trace))
        return trace
    def test_classic_and_fd_lengths_blocks_and_wrap(self):
        for fd,length in [(False,8),(True,12),(True,32),(True,64)]:
            for size in [1,7,8,10,31,62,63,140,4095]:
                for block in [0,1,8]:self.transfer(size,fd,length,block)
    def test_fd_single_frame_escape(self):
        trace=self.transfer(20,True,64)
        self.assertEqual(trace[0]["data"][:2],[0,20]);self.assertEqual(len(trace),1)
    def test_short_can_fd_single_frame_uses_regular_header(self):
        trace=self.transfer(3,True,64)
        self.assertEqual(trace[0]["data"][0],3)
        self.assertLessEqual(len(trace[0]["data"]),8)
        endpoint=IsoTp(0x708,0x700,True,64)
        with self.assertRaises(ValueError):endpoint.receive(dict(id=0x700,fd=True,data=[0,1,42]))
    def test_sequence_and_malformed_frame_errors(self):
        endpoint=IsoTp(0x708,0x700)
        with self.assertRaises(ValueError):endpoint.receive(dict(id=0x700,data=[7,1]))
        endpoint.receive(dict(id=0x700,data=[0x10,20,1,2,3,4,5,6]))
        with self.assertRaises(ValueError):endpoint.receive(dict(id=0x700,data=[0x22,1,2,3]))
    def test_wire_length_and_stmin(self):
        with self.assertRaises(ValueError):validate_frame(dict(id=0x123,fd=True,data=[0]*9))
        self.assertEqual(stmin_us(0xf1),100);self.assertEqual(stmin_us(127),127000)
        with self.assertRaises(ValueError):stmin_us(0x80)
    def test_pdu_bounds(self):
        endpoint=IsoTp(0x700,0x708)
        with self.assertRaises(ValueError):endpoint.send([])
        with self.assertRaises(ValueError):endpoint.send([1]*4096)

if __name__=="__main__":unittest.main()
