#include <stdio.h>
enum Op { OP_LOAD=0, OP_ADD=1, OP_MUL=2, OP_JMPZ=3, OP_HALT=4 };
struct VM { int stack[16]; int sp; int ip; int code[32]; int ncode; };
void push(struct VM *vm,int v){ vm->stack[vm->sp]=v; vm->sp=vm->sp+1; }
int pop(struct VM *vm){ vm->sp=vm->sp-1; return vm->stack[vm->sp]; }
int step_load(struct VM *vm){ int imm=vm->code[vm->ip]; vm->ip=vm->ip+1; push(vm,imm); return 1; }
int step_add(struct VM *vm){ int b=pop(vm); int a=pop(vm); push(vm,a+b); return 1; }
int step_mul(struct VM *vm){ int b=pop(vm); int a=pop(vm); push(vm,a*b); return 1; }
int step_jmpz(struct VM *vm){ int target=vm->code[vm->ip]; vm->ip=vm->ip+1; if(pop(vm)==0) vm->ip=target; return 1; }
int step_halt(struct VM *vm){ (void)vm; return 0; }
int (*handlers[5])(struct VM *);
int run(struct VM *vm){
  int alive=1;
  while(alive && vm->ip < vm->ncode){
    enum Op op=(enum Op)vm->code[vm->ip]; vm->ip=vm->ip+1;
    alive=(*handlers[op])(vm);
  }
  return (vm->sp>0)?vm->stack[vm->sp-1]:0;
}
int main(){
  struct VM vm; int i;
  handlers[OP_LOAD]=&step_load; handlers[OP_ADD]=&step_add;
  handlers[OP_MUL]=&step_mul; handlers[OP_JMPZ]=&step_jmpz; handlers[OP_HALT]=&step_halt;
  vm.sp=0; vm.ip=0; vm.ncode=0;
  for(i=0;i<16;i++) vm.stack[i]=0;
  vm.code[vm.ncode++]=OP_LOAD; vm.code[vm.ncode++]=3;
  vm.code[vm.ncode++]=OP_LOAD; vm.code[vm.ncode++]=4;
  vm.code[vm.ncode++]=OP_MUL;
  vm.code[vm.ncode++]=OP_LOAD; vm.code[vm.ncode++]=2;
  vm.code[vm.ncode++]=OP_ADD; vm.code[vm.ncode++]=OP_HALT;
  printf("%d\n", run(&vm)); return 0;
}
