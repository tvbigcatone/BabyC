int vm_global_counter = 3;
unsigned int vm_global_unsigned = 4294967294U;
unsigned long long vm_global_wide = 18446744073709551614ULL;

int main(void)
{
    vm_global_counter = vm_global_counter + 2;
    vm_global_counter++;
    vm_global_unsigned++;
    vm_global_wide++;
    return vm_global_counter == 6
        && vm_global_unsigned == 4294967295U
        && vm_global_wide == 18446744073709551615ULL ? 0 : 1;
}
