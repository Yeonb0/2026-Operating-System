SRCDIR = ..

# Test programs to compile, and a list of sources for each.
# To add a new test, put its name on the PROGS list
# and then add a name_SRC line that lists its source files.
PROGS = cat cmp cp echo halt hex-dump ls mcat mcp mkdir pwd rm shell \
	bubsort lineup matmult recursor additional

# Should work from project 2 onward.
cat_SRC = cat.c
cmp_SRC = cmp.c
cp_SRC = cp.c
echo_SRC = echo.c
halt_SRC = halt.c
hex-dump_SRC = hex-dump.c
lineup_SRC = lineup.c
ls_SRC = ls.c
recursor_SRC = recursor.c
rm_SRC = rm.c

# Should work in project 3; also in project 4 if VM is included.
bubsort_SRC = bubsort.c
matmult_SRC = matmult.c
mcat_SRC = mcat.c
mcp_SRC = mcp.c

# Should work in project 4.
mkdir_SRC = mkdir.c
pwd_SRC = pwd.c
shell_SRC = shell.c

# [1-1-8] 추가 시스템 콜 검증용 프로그램 등록
# 목적 : additional.c 를 사용자 프로그램으로 빌드한다
# 참고 : proj1 슬라이드 57 - examples/Makefile 을 수정해야 additional 이 빌드된다
# 주의 : PROGS 목록과 name_SRC 두 곳 모두에 등록해야 한다
#        PROGS 6행 끝의 recursor 뒤에 additional 을 붙였다
additional_SRC = additional.c

include $(SRCDIR)/Make.config
include $(SRCDIR)/Makefile.userprog