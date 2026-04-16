# Tamilator

Tamilator is a FPGA simulator built with C++ that can graphically simulate FPGA designs. It virtually simulates verilog design in an interactable graphical interface. It can read constraints.json files to map inputs and outputs to physical pins. It contains a 1kHz clock for now.

### Physical I/O

| Type              | Count | Pin Name | I/O    |
| ----------------- | ----- | -------- | ------ |
| Button            | 8     | BTN[0-7] | Input  |
| LED               | 8     | LED[0-7] | Output |
| Dip Switch (8bit) | 3     | DIP[0-2] | Input  |
| BarGraphs (8bit)  | 3     | BAR[0-2] | Output |
| Clock             | 1     | clock    | Input  |

### Constraint File Example

```json
{
    "inputs": {
        "BTN0": "btn",
        "BTN1": "rst"
    },
    "outputs": {
        "LED0": "led0",
        "LED1": "led1"
    },
    "bargraphs": {
        "BAR0": "counter"
    },
    "dipswitches": {
        "DIP0": "numA"
    },
    "clock": "clock"
}
```
