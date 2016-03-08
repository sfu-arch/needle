	.text
	.file	"buffer.ll"
	.globl	__prev_store_exists
	.align	16, 0x90
	.type	__prev_store_exists,@function
__prev_store_exists:                    # @__prev_store_exists
	.cfi_startproc
# BB#0:                                 # %entry
	pushl	%ebx
.Ltmp0:
	.cfi_def_cfa_offset 8
	pushl	%edi
.Ltmp1:
	.cfi_def_cfa_offset 12
	pushl	%esi
.Ltmp2:
	.cfi_def_cfa_offset 16
.Ltmp3:
	.cfi_offset %esi, -16
.Ltmp4:
	.cfi_offset %edi, -12
.Ltmp5:
	.cfi_offset %ebx, -8
	movl	20(%esp), %ecx
	movl	16(%esp), %edx
	leal	-16(%ecx), %esi
	.align	16, 0x90
.LBB0_1:                                # %while.cond
                                        # =>This Inner Loop Header: Depth=1
	xorl	%eax, %eax
	cmpl	%edx, %esi
	jb	.LBB0_3
# BB#2:                                 # %while.body
                                        #   in Loop: Header=BB0_1 Depth=1
	movl	(%esi), %edi
	movl	4(%esi), %ebx
	addl	$-16, %esi
	movl	$1, %eax
	xorl	4(%ecx), %ebx
	xorl	(%ecx), %edi
	orl	%ebx, %edi
	jne	.LBB0_1
.LBB0_3:                                # %return
	popl	%esi
	popl	%edi
	popl	%ebx
	retl
.Ltmp6:
	.size	__prev_store_exists, .Ltmp6-__prev_store_exists
	.cfi_endproc

	.globl	main
	.align	16, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# BB#0:                                 # %entry
	pushl	%edi
.Ltmp7:
	.cfi_def_cfa_offset 8
	pushl	%esi
.Ltmp8:
	.cfi_def_cfa_offset 12
	subl	$20, %esp
.Ltmp9:
	.cfi_def_cfa_offset 32
.Ltmp10:
	.cfi_offset %esi, -12
.Ltmp11:
	.cfi_offset %edi, -8
	movl	$0, undo_log+4
	movl	$1639, undo_log         # imm = 0x667
	movl	$0, undo_log+36
	movl	$1638, undo_log+32      # imm = 0x666
	movl	$undo_log+16, %eax
	movl	$undo_log, %ecx
	movl	$1638, %edx             # imm = 0x666
	.align	16, 0x90
.LBB1_1:                                # %while.cond.i
                                        # =>This Inner Loop Header: Depth=1
	xorl	%esi, %esi
	cmpl	%ecx, %eax
	jb	.LBB1_3
# BB#2:                                 # %while.body.i
                                        #   in Loop: Header=BB1_1 Depth=1
	movl	$1, %esi
	movl	(%eax), %edi
	xorl	%edx, %edi
	orl	4(%eax), %edi
	leal	-16(%eax), %eax
	jne	.LBB1_1
.LBB1_3:                                # %__prev_store_exists.exit
	movl	%esi, 4(%esp)
	movl	$.L.str, (%esp)
	calll	printf
	xorl	%eax, %eax
	addl	$20, %esp
	popl	%esi
	popl	%edi
	retl
.Ltmp12:
	.size	main, .Ltmp12-main
	.cfi_endproc

	.type	undo_log,@object        # @undo_log
	.comm	undo_log,96,16
	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"%d"
	.size	.L.str, 3


	.ident	"clang version 3.6.2 (tags/RELEASE_362/final)"
	.section	".note.GNU-stack","",@progbits
