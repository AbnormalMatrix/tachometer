import time
import machine
from machine import Pin
import rp2

input_pin = Pin(16, Pin.IN)

@rp2.asm_pio(in_shiftdir=rp2.PIO.SHIFT_LEFT, autopush=False)
def pulse_timer_pio():
    wrap_target()

    wait(0, pin, 0)
    wait(1, pin, 0)
    
    mov(x, invert(null))
    
    label("high_loop")
    jmp(pin, "still_high")
    jmp("falling_edge")
    
    label("still_high")
    jmp(x_dec, "high_loop")
    
    label("falling_edge")
    label("low_loop")
    jmp(pin, "done")

    jmp(x_dec, "low_loop")
    
    label("done")
    mov(isr, x)
    push(noblock)
    
    wrap()

clk_div = 125
sys_hz = machine.freq()
pio_hz = int(sys_hz / clk_div)
ns_per_count = 2_000_000_000.0 / pio_hz


sm = rp2.StateMachine(0, pulse_timer_pio, freq=pio_hz, in_base=input_pin, jmp_pin=input_pin)
sm.active(1)

while True:
    if sm.rx_fifo():
        value = 0xFFFFFFFF - (sm.get() & 0xFFFFFFFF)
        ns = value * ns_per_count
        rpm = 60_000_000_000 / (ns * 2)
        print(rpm)
    time.sleep_ms(10)
