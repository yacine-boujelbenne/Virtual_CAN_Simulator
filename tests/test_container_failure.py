"""CI check after Docker has stopped the motor container."""
import json,urllib.request,urllib.error
URL="http://127.0.0.1:8080/api/command"
def request(body):
    req=urllib.request.Request(URL,json.dumps(body).encode(),headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(req,timeout=20) as response:return json.load(response)
before=request({"action":"snapshot"})["time"]
try:request({"action":"step","count":1})
except urllib.error.HTTPError as error:
    assert error.code==503,error.code
else:raise AssertionError("A stopped ECU must not complete the barrier")
after=request({"action":"snapshot"})
assert after["time"]==before,(before,after["time"])
assert after["cluster"]["last_error"],after["cluster"]
assert sum(e["container"]["status"]=="connected" for e in after["ecus"])>=6
print("Stopped worker isolated; vehicle time did not advance and other ECU containers remain available")
