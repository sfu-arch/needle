; ModuleID = 'test.epp.bc'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@llvm.global_dtors = appending global [1 x { i32, void ()* }] [{ i32, void ()* } { i32 0, void ()* @PaThPrOfIlInG_save }]

; Function Attrs: nounwind readnone uwtable
define i32 @main(i32 %argc, i8** nocapture readnone %argv) #0 {
entry:
  tail call void @llvm.dbg.value(metadata i32 %argc, i64 0, metadata !13, metadata !18), !dbg !19
  tail call void @llvm.dbg.value(metadata i8** %argv, i64 0, metadata !14, metadata !18), !dbg !20
  call void @PaThPrOfIlInG_logPath()
  ret i32 0, !dbg !21
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.value(metadata, i64, metadata, metadata) #1

declare void @PaThPrOfIlInG_incCount(i64, i64, i64, i64)

declare void @PaThPrOfIlInG_logPath()

declare void @PaThPrOfIlInG_selfLoop(i64)

declare void @PaThPrOfIlInG_save()

attributes #0 = { nounwind readnone uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!15, !16}
!llvm.ident = !{!17}

!0 = !{!"0x11\0012\00clang version 3.6.2 (tags/RELEASE_362/final)\001\00\000\00\001", !1, !2, !2, !3, !2, !2} ; [ DW_TAG_compile_unit ] [/home/snehasish/working/pasha/test/01.c] [DW_LANG_C99]
!1 = !{!"01.c", !"/home/snehasish/working/pasha/test"}
!2 = !{}
!3 = !{!4}
!4 = !{!"0x2e\00main\00main\00\003\000\001\000\000\00256\001\003", !1, !5, !6, null, i32 (i32, i8**)* @main, null, null, !12} ; [ DW_TAG_subprogram ] [line 3] [def] [main]
!5 = !{!"0x29", !1}                               ; [ DW_TAG_file_type ] [/home/snehasish/working/pasha/test/01.c]
!6 = !{!"0x15\00\000\000\000\000\000\000", null, null, null, !7, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!7 = !{!8, !8, !9}
!8 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!9 = !{!"0xf\00\000\0064\0064\000\000", null, null, !10} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from ]
!10 = !{!"0xf\00\000\0064\0064\000\000", null, null, !11} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from char]
!11 = !{!"0x24\00char\000\008\008\000\000\006", null, null} ; [ DW_TAG_base_type ] [char] [line 0, size 8, align 8, offset 0, enc DW_ATE_signed_char]
!12 = !{!13, !14}
!13 = !{!"0x101\00argc\0016777219\000", !4, !5, !8} ; [ DW_TAG_arg_variable ] [argc] [line 3]
!14 = !{!"0x101\00argv\0033554435\000", !4, !5, !9} ; [ DW_TAG_arg_variable ] [argv] [line 3]
!15 = !{i32 2, !"Dwarf Version", i32 4}
!16 = !{i32 2, !"Debug Info Version", i32 2}
!17 = !{!"clang version 3.6.2 (tags/RELEASE_362/final)"}
!18 = !{!"0x102"}                                 ; [ DW_TAG_expression ]
!19 = !MDLocation(line: 3, column: 14, scope: !4)
!20 = !MDLocation(line: 3, column: 26, scope: !4)
!21 = !MDLocation(line: 4, column: 5, scope: !4)
