# Byte Queue Limits monitor

This program uses the Linux Byte Queue Limits support that some Linux Ethernet
drivers do support. Byte Queue Limits is refered to below as BQL. The program
provides a ncurses interface to monitor the current number of bytes in-flight in
the queue.

Motivation for this program was twofold:
- make sure that the Broadcom SYSTEMPORT driver did have correct BQL mechanism
  as I was writing it
- experiment and do something potentially useful with ncurses

# Snapshot

Here is a snapshot of a 32 transmit queue devices:

![Sample output](https://raw.githubusercontent.com/ffainelli/bqlmon/gh-pages/bqlmon_snapshot.png);

# BQL in a nutshell

BQL basically keeps track of how many bytes are in-flight, that are pending
transmission in a given Ethernet adapter hardware queue. The more information
about the actual hardware queue state we have, the better for software to
accomodate with transmission of network packets.

Ethernet drivers implementing that feature usually do the following in their
transmit function:

- prepare the buffer for transmission
- tell the network stack how many bytes are being sent (netdev_tx_sent_queue)
- tell the hardware the buffer should be transmitted

In their transmit complete function they do the following:

- count how many packets and bytes have been transmitted
- report how many packets and bytes have been completed
  (netdev_tx_completed_queue)
- free the transmitted buffers

Accurate reporting of how many buffers are in-flight allows for a better sizing
of all software queueing.

Using BQL also allows highlighting potential transmit queueing and flow control
issues. For instance, if the transmitted buffers are reclaimed in an interrupt
context, but that interrupt never fires, we will quickly queue up many buffers
that are not freed.

BQL exposes a few sysfs attributes in
/sys/class/net/*interface*/tx-*queue*/byte_queue_limits which are per-queue
information about:

- in-flight packets
- limit threshold until the queue is declared congested
- hold time

--
Florian Fainelli
