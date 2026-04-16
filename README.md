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
### Simulation of a counter design
![Counter Simulation Image](/images/image.png)

### Dependencies

1. Verilator
2. ImGUI with SDL2 backend
3. SDL2
4. Nlohmann JSON library

### Build and Run
```bash
g++ ./src/simulator.cpp -o tamilator
./tamilator top.v
```
**Note:** `top.v` is the top module of your verilog design. Currently it must be named top.v

----------

It's for personal use but you can use it if you want. Though I'm trying to make it into a installable software. But for now I'll focus on upgrading it ¯\\_(ツ)_/¯