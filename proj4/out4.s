.data
_prompt: .asciiz "Enter an integer:"
_ret:   .asciiz "\n"
.globl main
.text

read:
  li $v0, 4
  la $a0, _prompt
  syscall
  li $v0, 5
  syscall
  jr $ra

write:
  li $v0, 1
  syscall
  li $v0, 4
  la $a0, _ret
  syscall
  move $v0, $0
  jr $ra

main:
  subu $sp, $sp, 56
  sw $ra, 52($sp)
  sw $0, 28($sp)
  sw $0, 32($sp)
  sw $0, 36($sp)
  sw $0, 40($sp)
  sw $0, 44($sp)
  sw $0, 0($sp)
  sw $0, 4($sp)
  sw $0, 8($sp)
  sw $0, 12($sp)
  sw $0, 16($sp)
  sw $0, 20($sp)
  sw $0, 24($sp)
  li $t0, 0
  sw $t0, 0($sp)
label1:
  lw $t0, 0($sp)
  li $t1, 5
  blt $t0, $t1, label2
  j label3
label2:
  lw $t0, 0($sp)
  li $t1, 4
  mul $t2, $t0, $t1
  addi $t1, $sp, 28
  sw $t2, 4($sp)
  add $t2, $t1, $t2
  li $t1, 1
  add $t0, $t0, $t1
  sw $t0, 0($t2)
  sw $t0, 12($sp)
  sw $t2, 8($sp)
  lw $t0, 0($sp)
  li $t1, 1
  add $t0, $t0, $t1
  sw $t0, 0($sp)
  j label1
label3:
  li $t0, 0
  sw $t0, 0($sp)
label4:
  lw $t0, 0($sp)
  li $t1, 5
  blt $t0, $t1, label5
  j label6
label5:
  lw $t0, 0($sp)
  li $t1, 4
  mul $t0, $t0, $t1
  addi $t1, $sp, 28
  sw $t0, 16($sp)
  add $t0, $t1, $t0
  lw $t1, 0($t0)
  sw $t0, 20($sp)
  move $a0, $t1
  sw $t1, 24($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal write
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  lw $t0, 0($sp)
  li $t1, 1
  add $t0, $t0, $t1
  sw $t0, 0($sp)
  j label4
label6:
  li $v0, 0
  j __main_epilogue
__main_epilogue:
  lw $ra, 52($sp)
  addi $sp, $sp, 56
  jr $ra

