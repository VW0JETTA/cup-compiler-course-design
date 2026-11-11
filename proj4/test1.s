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
  subu $sp, $sp, 24
  sw $ra, 20($sp)
  sw $0, 0($sp)
  sw $0, 4($sp)
  sw $0, 8($sp)
  sw $0, 12($sp)
  sw $0, 16($sp)
  li $t0, 0
  li $t1, 1
  li $t2, 0
  sw $t0, 0($sp)
  sw $t1, 4($sp)
  sw $t2, 8($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal read
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  sw $t0, 12($sp)
label1:
  lw $t0, 8($sp)
  lw $t1, 12($sp)
  blt $t0, $t1, label2
  j label3
label2:
  lw $t0, 0($sp)
  lw $t1, 4($sp)
  add $t0, $t0, $t1
  move $a0, $t1
  sw $t0, 16($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal write
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  lw $t0, 4($sp)
  move $t1, $t0
  lw $t0, 16($sp)
  move $t2, $t0
  lw $t0, 8($sp)
  li $t3, 1
  add $t0, $t0, $t3
  sw $t0, 8($sp)
  sw $t1, 0($sp)
  sw $t2, 4($sp)
  j label1
label3:
  li $v0, 0
  j __main_epilogue
__main_epilogue:
  lw $ra, 20($sp)
  addi $sp, $sp, 24
  jr $ra

