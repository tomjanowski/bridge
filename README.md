# User space bridge (or maybe a wire)

This short project's goal was to create a simple program for forwarding traffic between two
interfaces, in a similar fashion as you would expect from a Linux bridge implementation. The difference
is that this program unconditionally forwards all the traffic between the two interfaces supplied from command line,
and I mean **all the traffic** (bridge eliminates some packets related to bridging protocols unless customized
via /proc interface). Therefore this program works more like a wire that is plugged in between two NICs.

The invocation is as follows:

> ./bridge interface1 interace2

It requires elevated priviledges to succeed.

The reason I needed this program was to connect a TAP device used by a virtual machine with a wireless interface
on a laptop. Wireless interfaces are notorious for not working well in bridges, mostly due to a constraint that
no other MAC address can be used that the one used for initial association with Access Point. A VM can send
packets with MAC address that is the same as the MAC of a wireless card, but I did not see any system-wide implementation how
to couple such a VM communicating from a tap device with a wireless interface. This program does not care about any MAC
addresses, it just simply shuffles traffic both ways.

Bridges would complain about existence of more than one device with the same MAC. Also, in many cases it is not even possible to add
a wifi card to a bridge on Linux. It is possible in so called "4 address mode" but then most AP will not accept this traffic.

This program uses Linux TX and RX RING implementation and raw Ethernet frames. It works pretty fast with negligible packet drops.
It will perform well on interfaces below 1 Gbit/s. I could not go beyond 5 Gbit/s on veth tests.

The program will need the following:

The virtual network device inside of a VM should be tuned:

> ethtool -K interfaceX tx off

to force the VM to fill in correct checksums for all the relevant protocols

The wireless interface should be prepared like this:

> ethtool -K wlp2s0 gro off

So that raw frames are as big as the ones received at the interface level (usually below 1500 bytes each). Otherwise TCP offloding might
combine a few frames to produce one big TCP segment.
