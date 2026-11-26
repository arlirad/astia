/*
 * Test-only OpenSSH server init.
 *
 * Configures the RTL8139 interface for QEMU user-mode networking, validates
 * the shipped sshd configuration, and then replaces itself with sshd.  The
 * kernel runs this program as /sbin/init only in the dedicated sshd test
 * initrd; normal Astia images never install it.
 */

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_ADDRESS "10.0.2.15"
#define TEST_NETMASK "255.255.255.0"
#define TEST_INTERFACE "eth0"

static int set_address(int fd, const char* name, unsigned long request, const char* address) {
    struct ifreq ifr;
    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;

    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, address, &sin->sin_addr) != 1)
        return -1;

    return ioctl(fd, request, &ifr);
}

static int configure_interface(void) {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("openssh-sshd-init: socket");
        return -1;
    }
    if (set_address(fd, TEST_INTERFACE, SIOCSIFADDR, TEST_ADDRESS) < 0 ||
        set_address(fd, TEST_INTERFACE, SIOCSIFNETMASK, TEST_NETMASK) < 0) {
        perror("openssh-sshd-init: interface address");
        close(fd);

        return -1;
    }

    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, TEST_INTERFACE, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("openssh-sshd-init: interface flags");
        close(fd);

        return -1;
    }
    ifr.ifr_flags = (short) (ifr.ifr_flags | IFF_UP | IFF_RUNNING);
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("openssh-sshd-init: interface up");
        close(fd);

        return -1;
    }
    close(fd);

    return 0;
}

static int run_command(char* const argv[]) {
    pid_t pid = fork();
    int status;

    if (pid < 0)
        return -1;
    if (pid == 0) {
        execve(argv[0], argv, environ);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;

    return status;
}

int main(void) {
    char* const test_argv[] = {"/usr/sbin/sshd", "-t", "-f",
                               "/etc/ssh/sshd_config", NULL};
    char* const daemon_argv[] = {"/usr/sbin/sshd", "-D", "-e", "-f",
                                 "/etc/ssh/sshd_config", NULL};
    int status;

    if (configure_interface() < 0) {
        puts("openssh-sshd-init: network configuration failed");
        return 1;
    }
    puts("openssh-sshd-init: network ready");
    status = run_command(test_argv);
    if (status < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        puts("openssh-sshd-init: sshd -t failed");
        return 1;
    }
    puts("openssh-sshd-init: ready");
    execve(daemon_argv[0], daemon_argv, environ);
    perror("openssh-sshd-init: execve sshd");

    return 1;
}
