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

fact:
  subu $sp, $sp, 24
  sw $ra, 20($sp)
  sw $0, 4($sp)
  sw $0, 8($sp)
  sw $0, 12($sp)
  sw $a0, 0($sp)
  lw $t0, 0($sp)
  li $t1, 1
  beq $t0, $t1, label1
  j label2
label1:
  lw $t0, 0($sp)
  move $v0, $t0
  j __fact_epilogue
label2:
  lw $t0, 0($sp)
  li $t1, 1
  sub $t2, $t0, $t1
  sw $t2, 4($sp)
  lw $t1, 4($sp)
  move $a0, $t1
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal fact
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  lw $t1, 0($sp)
  sw $t0, 8($sp)
  mul $t0, $t1, $t0
  sw $t0, 12($sp)
  move $v0, $t0
  j __fact_epilogue
__fact_epilogue:
  lw $ra, 20($sp)
  addi $sp, $sp, 24
  jr $ra

main:
  subu $sp, $sp, 16
  sw $ra, 12($sp)
  sw $0, 0($sp)
  sw $0, 4($sp)
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal read
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  sw $t0, 0($sp)
  lw $t0, 0($sp)
  li $t1, 1
  bgt $t0, $t1, label4
  j label5
label4:
  lw $t0, 0($sp)
  move $a0, $t0
  addi $sp, $sp, -4
  sw $ra, 0($sp)
  jal fact
  lw $ra, 0($sp)
  addi $sp, $sp, 4
  move $t0, $v0
  sw $t0, 4($sp)
  j label6
label5:
  li $t0, 1
  sw $t0, 4($sp)
label6:
  lw $t0, 4($sp)
  move $a0, $t0
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

