set can-use-hw-watchpoints 0
define connect
dir ~/cs3231/asst3-src/kern/compile/ASST3
target remote unix:.sockets/gdb
b panic
end
