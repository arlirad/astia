extern void test_out() {
    asm volatile("mov $0x3F8, %%dx; mov $'C', %%al; outb %%al, %%dx" ::: "dx", "al");
}
