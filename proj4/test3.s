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

sum:
  subu $sp, $sp, 16
  sw $ra, 12($sp)
  sw $0, 8($sp)
  sw $a0, 0($sp)
  sw $a1, 4($sp)
  lw $t0, 0($sp)
  lw $t1, 4($sp)
  add $t0, $t0, $t1
  sw $t0, 8($sp)
  move $v0, $t0
  j __sum_epilogue
__sum_epilogue:
  lw $ra, 12($sp)
  addi $sp, $sp, 16
  jr $ra

main:
  subu $sp, $sp, 16
  sw $ra, 12($sp)
  sw $0, 0($sp)
  sw $0, 4($sp)
  sw $0, 8($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal read
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  sw $t0, 0($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal read
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  sw $t0, 4($sp)
  lw $t0, 0($sp)
  move $a0, $t0
  lw $t1, 4($sp)
  move $a1, $t1
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal sum
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  move $a0, $t0
  sw $t0, 8($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal write
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  li $v0, 0
  j __main_epilogue
__main_epilogue:
  lw $ra, 12($sp)
  addi $sp, $sp, 16
  jr $ra

