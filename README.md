A simple project to list all of the network namespaces it can find.

Currently, does a scan of all PIDs and all bind-mounted namespaces visible in
the current namespace.

It outputs the namespace id (that is, its inode number) in hex and decimal,
then gives a way to access it, either via PID number, mountpoint, or both.


# Example

    $ sudo ./netnsfinder
    f000007b (4026531963) via 1
    f00001f1 (4026532337) via 1820
    f0000261 (4026532449) via 3770
    f00003a5 (4026532773) via 4669 or /run/docker/netns/07a20e70c6af
    f0000466 (4026532966) via 14885
    f00002cb (4026532555) via /run/docker/netns/ingress_sbox
    f0000328 (4026532648) via /run/docker/netns/1-c1itwam2i1
    f0000402 (4026532866) via /run/netns/testnet
    f0000462 (4026532962) via /tmp/my_net_bindmount

This shows several network namespaces. The first always exists, the network
namespace of init. Next in the above list are 4 namespaces reachable by a
process running in them. One of those is also a bind-mount by docker, which
indicates that pid is probably a container/contained process.

After that are 2 docker bind-mounts and one ip bind-mount ("ip netns...").

Finally, one bind-mount by-hand namespace.

For names under /run/netns (in this example: "testnet"), one can use the ip
command directly to inspect the network of the namespace.

list interfaces:

    $ ip netns exec testnet ip link show

list firewall:

    $ ip netns exec testnet iptables -vnL

For others, use the nsenter command, using --target PID to specify a PID
number, or --net=FILE to specify a bind-mount file.

list interfaces of given PID:

    $ nsenter --target 1820 --net ip link show

list interfaces of given mountpoint:

    $ nsenter --net=/run/docker/netns/ingress_show ip link show


# Building the project

The project is very simple. You must be on a Linux system, probably with
kernel at least 3.0. Only tested on 4.4.

simply run make:

    make


# Limitations

* If a network namespace is bindmounted in a mount namespace that isn't shared
  in the current namespace, and there are no processes holding that network
  namespace, then this program currently won't find it.

  This can especially happen when a container (e.g. lxc) is itself using
  network namespaces that have no running processes. This program would find
  it when run within the container, but not from the host.


* Only the first PID (and mountpoint) found is displayed for each network
  namespace. On a fresh system this is often the parent PID that is holding
  the namespace open (e.g. the unshare program, or daemon that starts
  containers, etc). However, this isn't garunteed. The listed PID may be a
  short-lived task in a container that no longer exists after it is displayed.


* If a process is holding a file descriptor for a network namespace, but is
  not part of that namespace, and the namespace isn't bind-mounted anywhere,
  this program won't find it.


# Future work

On the upside, with current kernels it seems impossible to bind-mount a mount
namespace, so future detection of the above should only involve a separate PID
walk looking for mount namespaces.
