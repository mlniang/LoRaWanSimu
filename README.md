# Simulation of a Grid Wireless Sensor Network based on LR-WPAN and IPv4 on NS3
This is a simple simulation of a grid LR-WPAN wireless sensor network organized in a mesh topology. Nodes send 74 bytes data over TCP to the gateway.
Default NS3 API does not support using TCP with LR-WPAN mac which use short mac address based on 2 bytes. So a modification was made to the original arp header implementation to support 2 bytes mac addresses (src/internet/model/arp-header.cc:139).
Animation and visualization is not properly implemented but we focused on the tracing.
