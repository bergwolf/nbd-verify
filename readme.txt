build
-----
make


usage
-----
nbd-verify -H host -P port -a action

where action is either:
	- verify
	- iops
	- latency

Also see the output of:
nbd-verify -h

The NBD server needs to serve 5GB of diskspace for verify to work and preferably more than the RAM size of the server on which the NBD server runs to measure the number of IOPS it can do (to prevent caching by the OS).

Please note that the verify as well as the IOPS test are destructive.

If all went fine, a message telling so is shown and the exit code is 0.
If not, messages informing about the problem are shwon and the exit code is 1.


Written by folkert@vanheusden.com
