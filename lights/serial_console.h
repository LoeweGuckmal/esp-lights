#pragma once

// Single-character test commands over the serial monitor (115200 baud).
void printSerialHelp();

// Call from loop().
void handleSerialCommands();
