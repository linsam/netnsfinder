A simple project to list all of the network namespaces it can find.

Currently, does a scan of all PIDs and all bind-mounted namespaces visible in
the current namespace.

It outputs the namespace id (that is, its inode number) in hex and decimal,
then gives a way to access it, either via PID number, mountpoint, or both.


# Example

    $ sudo ./netnsfinder
    Couldn't open /proc/mounts under /proc/46/ns/mnt: No such file or directory
    Couldn't open /proc/mounts under /proc/4669/ns/mnt: No such file or directory
    f000007b (4026531963) via 1
    f00001f1 (4026532337) via 1820
    f0000261 (4026532449) via 3770
    f00003a5 (4026532773) via 4669 or /run/docker/netns/07a20e70c6af
    f0000466 (4026532966) via 14885
    f00002cb (4026532555) via /run/docker/netns/ingress_sbox
    f0000328 (4026532648) via /run/docker/netns/1-c1itwam2i1
    f0000402 (4026532866) via /run/netns/testnet
    f0000462 (4026532962) via /tmp/my_net_bindmount
    f00004c2 (4026533058) via /tmp/blah (via /proc/18597/ns/mnt)

(We see a couple errors displayed. This indicates that there might be network
namespaces under mount namespaces (in the example, under PIDs 46 and 4669)
that we can't see.)

This shows several network namespaces. The first always exists, the network
namespace of init. Next in the above list are 4 namespaces reachable by a
process running in them (1820, 3770, 4669, and 14885). One of those is also a
bind-mount by docker, which indicates that pid is probably a container or
contained process.

After that are 2 docker bind-mounts (ingress_sbox and 1-c1itwam2i1.

After that is one ip bind-mount (made with "ip netns add...") called testnet

After that, one bind-mount by-hand namespace (/tmp/my_net_bindmount).

Finally, there is a hand mounted namespace at /tmp/blah, but not visible in
the current mount namespace. It is at least visible via the mount namespace of
PID 18597.

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

The sub namespaces are more complicated. You need to nsenter the outer mount
namespace to then nsenter the contained net namespace before running the
actual command:

    $ nsenter --mount=/proc/18597/ns/mnt nsenter --net=/tmp/blah ip link show


# Building the project

The project is very simple. You must be on a Linux system, probably with
kernel at least 3.0. Only tested on 4.4.

simply run make:

    make


# Limitations

* If a network namespace is bindmounted in a mount namespace that isn't shared
  in the current namespace, and there are no processes holding that network
  namespace, and that mount namespace doesn't have /proc mounted, then this
  program currently won't find it.


* Only the first PID (and mountpoint) found is displayed for each network
  namespace. On a fresh system this is often the parent PID that is holding
  the namespace open (e.g. the unshare program, or daemon that starts
  containers, etc). However, this isn't garunteed. The listed PID may be a
  short-lived task in a container that no longer exists after it is displayed.


* If a process is holding a file descriptor for a network namespace, but is
  not part of that namespace, and the namespace isn't bind-mounted anywhere,
  this program won't find it.


# Future work

When scanning mount namespaces, if we find one that doesn't have proc mounted,
the unshare that mount namespace and try to mount /proc ourselves. This could
possibly be tharted by the namespace having /proc as a regular file, or
lacking a /proc and having a read only root.
