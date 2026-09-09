# Pintos Project #1 보고서 작성용 기록

CSE4070 운영체제 / 2026 Fall
`document.docx` 양식의 각 절에 그대로 옮겨 쓸 수 있도록 단계별로 누적 기록합니다.

---

## 보고서 항목 ↔ 단계 대응표

| 보고서 항목 | 해당 단계 |
| --- | --- |
| Argument Passing | 1-1-2 |
| User Memory Access | 1-1-3 |
| System Calls | 1-1-4 ~ 1-1-7 |
| File Descriptor | 1-2-1 ~ 1-2-5 |
| Synchronization in Filesystem | 1-2-7 |
| Additional System Calls | 1-1-8 |

---

## 1-1-1 환경 구축 및 베이스라인 확인

### 개발 환경

| 항목 | 내용 |
| --- | --- |
| 편집 | 로컬 Windows + VS Code (WSL 확장) |
| 빌드 / 실행 | WSL2 + Ubuntu 22.04 + QEMU (qemu-system-i386) |
| 최종 검증 | cspro9.sogang.ac.kr (계정 발급 후 적용 예정) |
| 소스 | e-class 배포본 pintos_modified.tar.gz |

### IV.B 개발상 발생한 문제와 해결책

- 문제 : 조교 안내 자료(install 슬라이드 30)에서 CSPRO 서버에 VS Code 사용을 금지하고 있어, VS Code 를 그대로 쓰면서 서버 정책을 지킬 구성이 필요했다
- 해결 : 로컬 WSL2 에 동일한 QEMU 기반 실행 환경을 구축해 개발과 1차 검증을 수행하고, 채점 기준인 cspro9 에서 재검증하는 이중 구조를 채택했다
- 부가 확인 : 배포본의 src/utils/pintos 621행이 qemu-system-i386 을 직접 호출하므로, 최신 우분투에서 별도의 심볼릭 링크 없이 동작함을 확인했다

### IV.D 시험 및 평가 (baseline)

- 구현 전 make check 결과 : 0 / 80 (docs/verify/1-1-1-baseline.txt)
- 구현 전 echo 실행 결과 : `Executing 'echo x':` 다음에 `x` 가 출력되지 않고 종료
- 근거 : proj1 슬라이드 5, 10 — 시스템 콜과 사용자 스택 미구현으로 인한 정상 상태
- 채점 대상은 76개(Prj1-1 21개 / Prj1-2 55개)이며 make check 가 실행하는 80개와 다르다 (proj1 슬라이드 84). 최종 판정은 make grade 기준

---

## 1-1-2 Argument Passing

### II.A 구현 이유와 기대 결과

- 이유 : 기존 Pintos 는 명령행 전체를 실행 파일 이름으로 취급하므로 인자가 포함된 프로그램을 열 수 없고, 사용자 스택에 인자를 전달하는 과정이 없어 argc / argv 가 구성되지 않는다
- 기대 결과 : args-none, args-single, args-multiple, args-many, args-dbl-space 5개 테스트 통과

### 테스트가 요구하는 동작

| 테스트 | 전달 인자 | 기대 argc |
| --- | --- | --- |
| args-none | 없음 | 1 |
| args-single | onearg | 2 |
| args-multiple | some arguments for you! | 5 |
| args-many | a b c ... v (22개) | 23 |
| args-dbl-space | two  spaces! (공백 2개) | 3 |

확정된 요구사항

1. argv[0] 은 프로그램 이름만 담아야 한다
2. argv[argc] 는 NULL 이어야 한다 (args.c 가 i <= argc 로 순회)
3. 종료 메시지의 이름도 프로그램 이름만 사용해야 한다
4. 연속 공백은 하나의 구분자로 취급해야 한다 (manual 3.3.3)

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/process.c |
| 수정 함수 | process_execute (), load (), process_wait () |
| 추가 함수 | construct_stack (const char *cmdline, void **esp) — static |
| 추가 자료구조 | Prog_name (char [128]), Cmd_copy (char [128]), Argv (char *[64]), MAX_CMD_LEN, MAX_ARGC |
| 사용한 내장 함수 | strlcpy (), strtok_r (), strlen (), memcpy () — lib/string.c |

변경 내용

1. process_execute () : 명령행 사본에서 첫 토큰만 잘라 thread_create () 의 이름 인자로 넘긴다. 스레드 이름이 1-1-5 의 Process Termination Message 에 쓰이기 때문이다
2. load () : filesys_open () 에 넘기는 이름도 첫 토큰으로 교체한다. 기존 코드는 "echo a b" 라는 이름의 파일을 찾으므로 항상 실패한다
3. load () : setup_stack () 성공 직후 construct_stack () 을 호출해 인자를 스택에 쌓는다
4. process_wait () : 임시 대기 루프로 변경 (manual 3.2 권고). 1-1-7 에서 정식 구현으로 교체할 부채다

설계 판단

- strtok_r () 은 대상 문자열을 직접 변형하므로, const 인 file_name 과 load () 로 전달되는 fn_copy 는 대상으로 쓸 수 없어 지점마다 별도 사본 버퍼를 두었다
- 연속 공백 처리는 strtok_r () 이 구분자를 연속으로 건너뛰는 성질로 자동 해결되어 별도 코드가 필요 없었다
- 명령행 길이 한계는 manual 3.3.3 이 언급한 128바이트를 따랐고, 그 안에서 만들 수 있는 최대 토큰 수를 고려해 argv 배열을 64개로 잡았다

### IV.A Flow Chart 소재

```
run_task()  [threads/init.c]
  └─ process_execute(task)  [userprog/process.c]
       ├─ palloc_get_page() → fn_copy 에 명령행 전체 복사
       ├─ strlcpy + strtok_r → Prog_name 에 첫 토큰만 추출
       ├─ thread_create(Prog_name, ..., start_process, fn_copy)
       └─ start_process()
            └─ load(file_name, &if_.eip, &if_.esp)
                 ├─ strtok_r → filesys_open(프로그램 이름)
                 ├─ setup_stack(esp)          : 빈 4KB 스택 페이지 할당
                 └─ construct_stack(cmdline, esp) : argc / argv 구성
```

### IV.B 제작 내용 — 스택 구성 알고리즘

setup_stack () 이 \*esp 를 PHYS_BASE 로 설정한 직후, 다음 순서로 데이터를 쌓는다 (manual 3.5 80x86 Calling Convention).

1. 명령행 사본을 만들어 strtok_r () 로 토큰을 분리하고 argc 를 센다
2. 인자 문자열을 역순으로 push 하면서, 각 문자열이 놓인 사용자 스택 주소를 Argv 에 다시 기록한다
3. esp 를 4의 배수로 내림 정렬한다 (word align). 패딩 바이트는 0으로 채운다
4. argv[argc] 자리에 null sentinel 4바이트를 push 한다
5. Argv 에 기록해 둔 주소들을 역순으로 push 한다
6. argv[0] 이 놓인 주소(argv)를 push 한다
7. argc 를 push 한다
8. fake return address 4바이트를 push 한다

설계 판단

- 문자열을 역순으로 push 해야 argv[0] 이 높은 주소에 오고, hex_dump 결과가 매뉴얼 예시 배치와 일치한다
- null sentinel 이 없으면 args.c 의 `for (i = 0; i <= argc; i++)` 순회에서 argv[argc] 가 쓰레기 값을 가리켜 테스트가 실패한다
- fake return address 는 실제로 사용되지 않지만, 이것이 없으면 \_start () 가 읽는 argc, argv 의 오프셋이 4바이트씩 어긋난다

### IV.B 개발상 발생한 문제와 해결책

문제 1 — 커널 출력이 전혀 보이지 않음

- 증상 : 코드를 수정해도 load 실패 메시지 등 커널 내부 출력이 화면에 나타나지 않아 동작 확인이 불가능했다
- 원인 : process_wait () 의 초기 구현이 즉시 -1 을 반환하므로, 부모가 곧바로 run_task () 를 빠져나가 자식 프로세스가 한 번도 스케줄되지 않은 채 Pintos 가 종료되었다 (실행 결과의 "0 user ticks" 로 확인)
- 해결 : manual 3.2 의 권고에 따라 process_wait () 을 임시 대기 루프로 변경해 자식이 실행될 시간을 확보했다

문제 2 — 사용자 영역 page fault

- 증상 : 실행 파일 이름 분리까지 마친 뒤 echo 를 실행하자 `Page fault at 0xc0000008: rights violation error reading page in user context` 가 발생했다
- 원인 : setup_stack () 은 빈 스택 페이지만 만들기 때문에, lib/user/entry.c 의 \_start () 가 argc 와 argv 를 읽으려 할 때 PHYS_BASE 위쪽인 커널 영역(0xc0000008)을 참조하게 된다
- 해결 : construct_stack () 을 추가해 80x86 calling convention 대로 인자를 쌓고, \_start () 가 올바른 위치에서 argc / argv 를 읽도록 했다
- 부가 : 이 page fault 는 실행 파일이 정상적으로 load 되어 사용자 코드(eip=0x8048110)까지 진입했음을 보여 주는 근거이기도 하다

### IV.D 시험 및 평가 — 스택 배치 검증

`pintos --filesys-size=2 -p ../examples/echo -a echo -- -f -q run 'echo x'` 실행 시 hex_dump 결과

```
bfffffe0  00 00 00 00 02 00 00 00-ec ff ff bf f9 ff ff bf |................|
bffffff0  fe ff ff bf 00 00 00 00-00 65 63 68 6f 00 78 00 |.........echo.x.|
```

해석 (PHYS_BASE = 0xc0000000)

| 주소 | 값 | 의미 |
| --- | --- | --- |
| 0xbfffffe0 | 0x00000000 | fake return address, 최종 esp |
| 0xbfffffe4 | 0x00000002 | argc = 2 |
| 0xbfffffe8 | 0xbfffffec | argv (argv[0] 이 놓인 주소) |
| 0xbfffffec | 0xbffffff9 | argv[0] → "echo" |
| 0xbffffff0 | 0xbffffffe | argv[1] → "x" |
| 0xbffffff4 | 0x00000000 | argv[2] = null sentinel |
| 0xbffffff8 | 0x00 | word align 패딩 1바이트 |
| 0xbffffff9 | echo\0 | 인자 문자열 5바이트 |
| 0xbffffffe | x\0 | 인자 문자열 2바이트 |

manual 3.5 의 예시 배치와 일치함을 확인했다. 이어서 출력된 `system call!` 은 echo 가 printf 를 호출하며 write 시스템 콜을 요청했으나 syscall_handler 골격이 메시지만 출력하고 종료하기 때문이며, 인자 전달이 사용자 프로그램까지 정상적으로 도달했음을 뜻한다.

검증 후 hex_dump 호출은 제거했다. manual 3.3.2 가 Pintos 가 원래 출력하지 않는 메시지는 채점 스크립트를 혼란시킨다고 명시하기 때문이다.

---

## 1-1-3 User Memory Access

### II.A 구현 이유와 기대 결과

- 이유 : 사용자 프로그램은 커널에 임의의 포인터를 넘길 수 있다. 커널이 이를 검증 없이 역참조하면 커널 영역을 침범하거나 매핑되지 않은 페이지를 건드려 커널 패닉으로 이어진다
- 기대 결과 : 잘못된 포인터를 넘긴 프로세스만 종료되고 커널은 계속 동작한다. bad-read, bad-write, sc-bad-sp, sc-bad-arg 대응 기반이 마련된다

### II.B invalid memory access 개념

Pintos 에서 사용자 주소 공간은 0 부터 PHYS_BASE (0xc0000000) 직전까지이고, 그 위는 커널 영역이다 (threads/vaddr.h). 다음 세 경우가 invalid memory access 에 해당한다.

1. 널 포인터 : bad-read 는 `*(volatile int *) NULL` 을 읽고, bad-write 는 같은 주소에 쓴다
2. PHYS_BASE 이상의 주소 : sc-bad-arg 는 esp 를 0xbffffffc 로 두어 esp 자체는 유효하지만 인자를 읽을 esp + 4 가 커널 영역이 된다
3. 매핑되지 않은 사용자 주소 : sc-bad-sp 는 esp 를 코드 영역보다 약 64 MB 아래로 옮긴다

### II.B 어떻게 막을 것인가

시스템 콜을 통해 커널로 들어온 모든 주소를 사용 전에 검사한다. 검사 항목은 널 여부, is_user_vaddr () 로 PHYS_BASE 미만인지, pagedir_get_page () 로 현재 프로세스에 매핑되어 있는지 세 가지다. 하나라도 어긋나면 해당 프로세스를 종료한다.

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | check_address (const void \*addr, unsigned size) |
| 수정 함수 | syscall_handler () |
| 사용한 내장 함수 | is_user_vaddr (), pg_round_down () — threads/vaddr.h, pagedir_get_page () — userprog/pagedir.h |

설계 판단

- 단일 주소가 아니라 (시작 주소, 크기) 쌍을 받도록 만들었다. 4바이트 정수뿐 아니라 이후 read / write 의 버퍼 검사에도 같은 함수를 재사용하기 위해서다
- 접근 범위가 두 페이지에 걸칠 수 있으므로 걸쳐 있는 모든 페이지를 확인한다. sc-boundary 는 시스템 콜 번호와 인자가 서로 다른 페이지에 있어도 정상 동작해야 하므로 "같은 페이지" 조건을 걸면 안 된다
- threads/vaddr.h 는 수정하지 않았다. is_user_vaddr () 가 이미 필요한 검사를 제공하기 때문이다
- 현재 종료 수단은 thread_exit () 뿐이며, 1-1-5 에서 exit(-1) 로 교체해야 테스트가 기대하는 종료 코드가 나온다

### IV.A Flow Chart 소재

```
사용자 프로그램
  └─ int $0x30
       └─ intr_handler()  [threads/interrupt.c]
            └─ syscall_handler(f)  [userprog/syscall.c]
                 └─ check_address(f->esp, 4)
                      ├─ addr == NULL              → 종료
                      ├─ !is_user_vaddr(addr)      → 종료
                      ├─ pagedir_get_page() == NULL → 종료
                      └─ 통과 시 시스템 콜 처리 계속
```

---

## 1-1-4 System Call Handler 골격

### II.B 시스템 콜의 필요성

사용자 프로그램은 커널 영역에 직접 접근할 수 없으므로, 콘솔 출력이나 파일 접근처럼 커널 권한이 필요한 작업은 시스템 콜로 요청해야 한다. Pintos 에서는 사용자 프로그램이 `int $0x30` 인터럽트를 발생시키고, 부팅 시 등록된 syscall_handler () 가 이를 처리한다.

### 호출 흐름 (proj1 슬라이드 25 - 28, 46 - 52)

1. lib/user/syscall.c 의 API 가 인자와 시스템 콜 번호를 사용자 스택에 쌓고 `int $0x30` 실행
2. threads/intr-stubs.S 가 인터럽트 프레임을 구성
3. threads/interrupt.c 의 intr_handler () 가 등록된 핸들러 호출
4. userprog/syscall.c 의 syscall_handler () 가 f->esp 로 스택에 접근
5. 처리 결과는 80x86 관례에 따라 f->eax 에 저장되어 사용자에게 돌아간다

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | get_argument (void \*esp, int \*Arg, int count) |
| 수정 함수 | syscall_handler () |
| 추가 자료구조 | Arg — 각 시스템 콜 구현부에서 선언하는 인자 배열 |

변경 내용

- f->esp 가 가리키는 4바이트에서 시스템 콜 번호를 읽는다
- lib/syscall-nr.h 의 enum 값(SYS_HALT = 0, SYS_EXIT = 1, ... , SYS_WRITE = 9)으로 switch 분기한다
- get_argument () 는 번호 바로 위 칸부터 인자를 순서대로 꺼내며, 각 인자를 읽기 전에 check_address () 로 검증한다

설계 판단

- 인자 추출을 별도 함수로 분리했다. 모든 시스템 콜이 같은 규칙으로 인자를 꺼내므로 중복을 줄이고, 검증 누락을 구조적으로 막을 수 있다
- 포인터 인자는 여기서 값만 가져오고, 그 포인터가 가리키는 대상의 유효성은 각 시스템 콜 구현부에서 별도로 검사한다. 버퍼 길이가 시스템 콜마다 다르기 때문이다

### 각 case 의 구현 담당 단계

| 시스템 콜 | 단계 |
| --- | --- |
| halt, exit | 1-1-5 |
| read, write | 1-1-6 |
| exec, wait | 1-1-7 |
| create, remove, open, close, filesize, seek, tell | 1-2 |

---

## 1-1-5 halt, exit 및 Process Termination Message

### II.B 시스템 콜 설명

- halt : shutdown_power_off () 를 호출해 Pintos 를 즉시 종료한다. 매뉴얼 3.3.2 에 따라 이때는 종료 메시지를 출력하지 않는다
- exit : 종료 상태를 스레드에 기록하고 프로세스를 끝낸다. 관례상 0 이 정상 종료, 0 이 아닌 값은 오류를 뜻한다

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/threads/thread.h, src/userprog/process.c, src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 자료구조 | struct thread 의 int exit_status |
| 추가 함수 | halt (void), exit (int status) |
| 수정 함수 | check_address (), syscall_handler (), start_process (), process_exit () |
| 사용한 내장 함수 | shutdown_power_off () — devices/shutdown.h |

변경 내용

1. thread.h : struct thread 에 exit_status 필드를 추가했다. 프로세스 이름을 struct thread 에서 가져오라는 proj1 슬라이드 32 의 안내에 맞춰 종료 상태도 같은 구조체에 두었다
2. start_process () : exit_status 초기값을 -1 로 설정한다
3. process_exit () : pd != NULL 인 경우에만 `이름: exit(상태)` 형식으로 출력한다
4. syscall.c : SYS_HALT, SYS_EXIT 분기를 채우고, check_address () 의 종료 경로를 thread_exit () 에서 exit (-1) 로 교체했다

설계 판단

- 종료 메시지 출력 지점을 process_exit () 한 곳으로 모았다. 정상 exit 뿐 아니라 page fault 로 인한 강제 종료도 결국 thread_exit () → process_exit () 을 거치므로, 출력 지점을 한 곳에 두면 메시지 누락과 중복을 동시에 막을 수 있다
- 강제 종료 시의 -1 을 exception.c 수정으로 처리하지 않고 exit_status 초기값으로 해결했다. exception.c 는 Project 3 에서 가상 메모리 때문에 다시 손대야 하는 파일이라, 지금 단계에서 불필요한 변경을 남기지 않는 편이 낫다고 판단했다
- pd != NULL 조건 안에 출력을 둔 이유는 매뉴얼 3.3.2 가 사용자 프로세스가 아닌 커널 스레드 종료 시에는 메시지를 출력하지 말라고 명시하기 때문이다
- halt 는 shutdown_power_off () 가 곧바로 전원을 내리므로 process_exit () 자체가 호출되지 않는다. 별도 분기가 필요 없다

### IV.A Flow Chart 소재

```
사용자 프로그램
  └─ exit(status)  [lib/user/syscall.c]
       └─ int $0x30
            └─ syscall_handler()  [userprog/syscall.c]
                 ├─ check_address(f->esp, 4)
                 ├─ syscall_number = *(int *) f->esp
                 └─ case SYS_EXIT
                      ├─ get_argument(f->esp, Arg, 1)
                      └─ exit(Arg[0])
                           ├─ thread_current()->exit_status = status
                           └─ thread_exit()  [threads/thread.c]
                                └─ process_exit()  [userprog/process.c]
                                     └─ printf("%s: exit(%d)\n", ...)
```

---

## 1-1-6 read, write (표준 입출력 전용)

### II.B 시스템 콜 설명

- write : fd 가 1(STDOUT) 일 때 버퍼 내용을 콘솔에 출력하고 쓴 바이트 수를 반환한다. 처리할 수 없는 fd 이면 -1 을 반환한다
- read : fd 가 0(STDIN) 일 때 키보드에서 요청한 바이트 수만큼 읽어 버퍼에 채우고 읽은 바이트 수를 반환한다. 처리할 수 없는 fd 이면 -1 을 반환한다

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | int write (int fd, const void \*buffer, unsigned size), int read (int fd, void \*buffer, unsigned size) |
| 수정 함수 | syscall_handler () |
| 사용한 내장 함수 | putbuf () — lib/kernel/console.c, input_getc () — devices/input.c |

변경 내용

- SYS_READ, SYS_WRITE 분기를 채우고 반환값을 f->eax 에 저장한다 (proj1 슬라이드 28)
- switch 뒤에 있던 무조건 thread_exit () 을 제거했다. 구현된 시스템 콜은 사용자 프로그램으로 정상 복귀해야 하기 때문이다
- 아직 구현되지 않은 SYS_EXEC, SYS_WAIT 와 지원하지 않는 번호(default)에서만 종료한다

설계 판단

- 버퍼 포인터는 사용자가 넘긴 값이므로 사용 전에 check_address () 로 (시작 주소, 크기) 전체를 검증한다. 특히 read 는 커널이 사용자 메모리에 쓰기를 하므로 검증 누락의 위험이 크다
- fd 상수는 직접 숫자를 쓰지 않고 lib/stdio.h 의 STDIN_FILENO, STDOUT_FILENO 를 사용해 의도를 드러냈다
- 1-2 단계에서 fd >= 2 인 파일 입출력으로 확장할 때 함수 시그니처를 그대로 두고 분기만 추가할 수 있도록 구조를 잡았다

### 이 단계의 의미

echo 가 printf 를 통해 요청하는 write 가 처음으로 실제 동작한다. 즉 `run 'echo x'` 에서 인자 x 가 화면에 출력되기 시작하며, 1-1-2 에서 구성한 사용자 스택이 끝까지 올바르게 전달되었음을 사용자 눈높이에서 확인할 수 있게 된다.

---

## 1-1-7 exec, wait

### II.B 시스템 콜 설명

- exec : 명령행 문자열이 가리키는 프로그램을 새 프로세스로 실행하고 자식의 pid 를 반환한다. 프로그램을 적재할 수 없으면 -1 을 반환한다
- wait : 지정한 자식이 종료할 때까지 기다린 뒤 그 종료 상태를 반환한다. 자식이 아니거나 이미 기다린 적이 있는 pid 이면 기다리지 않고 -1 을 반환한다

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/threads/thread.h, src/threads/thread.c, src/userprog/process.c, src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 자료구조 | struct thread 의 Parent, Child_list, Child_elem, is_loaded, Load_sema, Exit_sema, Destroy_sema |
| 추가 함수 | exec (), wait (), check_string (), get_child_process () |
| 수정 함수 | init_thread (), thread_create (), process_execute (), start_process (), process_wait (), process_exit () |
| 사용한 내장 함수 | sema_init (), sema_down (), sema_up () — threads/synch.h, list_push_back (), list_remove (), list_entry () — lib/kernel/list.h |

### IV.B 제작 내용 — 부모 - 자식 동기화 설계

세마포어 세 개를 쓰며, 각각 기다리는 주체와 시점이 다르다.

| 세마포어 | 올리는 쪽 | 내리는 쪽 | 역할 |
| --- | --- | --- | --- |
| Load_sema | 자식 (start_process) | 부모 (process_execute) | 실행 파일 적재 성공 여부 확인 |
| Exit_sema | 자식 (process_exit) | 부모 (process_wait) | 자식 종료 대기 |
| Destroy_sema | 부모 (process_wait 또는 process_exit) | 자식 (process_exit) | 종료 상태를 넘겨줄 때까지 struct thread 유지 |

동작 순서

1. process_execute () 가 thread_create () 로 자식을 만들고 Load_sema 를 내려 대기한다
2. 자식은 start_process () 에서 load () 결과를 is_loaded 에 기록하고 Load_sema 를 올린다
3. 부모는 깨어나 is_loaded 가 거짓이면 TID_ERROR 를 반환한다. exec 의 반환값 -1 이 여기서 결정된다
4. 자식이 종료하면 process_exit () 이 종료 메시지를 출력하고 Exit_sema 를 올린 뒤 Destroy_sema 에서 대기한다
5. 부모는 process_wait () 에서 Exit_sema 를 내려 깨어나고, exit_status 를 읽은 뒤 자식을 목록에서 제거하고 Destroy_sema 를 올린다
6. 자식이 깨어나 스레드 소멸 절차를 마친다

설계 판단

- 종료 상태를 읽기 전에 Destroy_sema 를 올리면 안 된다. 자식이 깨어나는 순간 struct thread 가 담긴 페이지가 해제될 수 있기 때문이다
- 회수한 자식은 Child_list 에서 제거한다. 같은 pid 로 두 번째 wait 을 호출하면 get_child_process () 가 NULL 을 돌려주어 자연히 -1 이 된다 (wait-twice)
- 부모가 wait 없이 먼저 종료하는 경우를 대비해, process_exit () 이 자신의 자식들을 순회하며 Destroy_sema 를 미리 올린다. 이렇게 하지 않으면 자식이 영원히 대기한다
- Destroy_sema 대기는 사용자 프로세스에만 적용한다. 커널 스레드까지 대기시키면 부모가 없어 Pintos 가 전원을 내리지 못한다
- 부모 등록은 init_thread () 가 아니라 thread_create () 에서 한다. thread_init () 이 최초 스레드에 대해 init_thread () 를 부를 때는 아직 thread_current () 를 안전하게 쓸 수 없기 때문이다
- exec 의 문자열 인자는 check_string () 으로 널 종료까지 한 바이트씩 검증한다. 페이지 경계를 넘어갈 수 있어 시작 주소만 검사하면 exec-bad-ptr 를 막지 못한다

### 임시 코드 정리

1-1-2 에서 넣었던 process_wait () 의 `for (;;) thread_yield ()` 대기 루프를 제거했다. 이제 run_task () 가 정상적으로 반환되어 Pintos 가 스스로 전원을 내리며, 실행 후 Ctrl + C 로 강제 종료할 필요가 없다.

---

## 1-1-7 시점 중간 검증 결과

### IV.D 시험 및 평가 — Prj1-1 대상 21개 테스트

1-1-7 완료 시점에 `make check` 를 수행한 결과, Prj1-1 채점 대상 21개가 모두 통과했다.

Functionality 13개

```
pass tests/userprog/args-none
pass tests/userprog/args-single
pass tests/userprog/args-multiple
pass tests/userprog/args-many
pass tests/userprog/args-dbl-space
pass tests/userprog/exec-once
pass tests/userprog/exec-arg
pass tests/userprog/exec-multiple
pass tests/userprog/wait-simple
pass tests/userprog/wait-twice
pass tests/userprog/multi-recurse
pass tests/userprog/exit
pass tests/userprog/halt
```

Robustness 8개

```
pass tests/userprog/sc-bad-sp
pass tests/userprog/sc-bad-arg
pass tests/userprog/sc-boundary
pass tests/userprog/sc-boundary-2
pass tests/userprog/exec-missing
pass tests/userprog/exec-bad-ptr
pass tests/userprog/wait-bad-pid
pass tests/userprog/wait-killed
```

전체 결과는 80개 중 35개 실패이며, 실패한 항목은 모두 파일 시스템 관련(create, open, read, write, close, rox 계열과 tests/filesys/base/\*)으로 Prj1-2 범위다.

특히 배점이 가장 큰 multi-recurse (15점) 가 통과한 점이 의미 있다. exec 과 wait 을 재귀적으로 반복하는 테스트이므로, 1-1-7 에서 설계한 세 세마포어 구조가 중첩된 부모 - 자식 관계에서도 교착 없이 동작함을 확인할 수 있다.

증거 파일

- docs/verify/1-1-7-check.txt : make check 전체 출력
- docs/verify/1-1-7-grade.txt : make grade 결과

---

## 캡처 보관 위치

- docs/verify/1-1-1-baseline.txt : 구현 전 make check 결과
- docs/verify/ : 단계별 검증 화면 캡처를 1-x-y.png 형식으로 누적

## 남은 임시 코드

- 없음 (process_wait () 의 임시 대기 루프는 1-1-7 에서 제거됨)

## 1-1-8 Additional System Calls (fibonacci, max_of_four_int)

보고서 대응 항목 : IV.F Additional System calls, V.A 시험 및 평가 내용
근거 : proj1 슬라이드 54 - 58, 82 / Pintos manual 3.2, 3.5

### 수정하거나 추가한 파일

| 파일 | 내용 |
| --- | --- |
| src/lib/syscall-nr.h | enum 끝에 SYS_FIBONACCI(20), SYS_MAX_OF_FOUR_INT(21) 추가 |
| src/lib/user/syscall.h | 사용자 API 프로토타입 2개 추가 |
| src/lib/user/syscall.c | syscall4 매크로 정의, fibonacci / max_of_four_int API 정의 |
| src/userprog/syscall.h | 커널 측 구현 프로토타입 2개 추가 |
| src/userprog/syscall.c | 두 함수 구현, Arg[3] → Arg[4], switch 분기 2개 추가 |
| src/examples/additional.c | 신규 사용자 프로그램 |
| src/examples/Makefile | PROGS 와 additional_SRC 에 등록 |

### 설계상 판단한 것

1. 시스템 콜 번호는 enum 의 맨 끝에 붙였다. 중간에 삽입하면 기존 번호가 전부
   밀려 이미 통과한 21개 테스트가 한꺼번에 깨지기 때문이다.
2. max_of_four_int 는 인자가 4개인데 배포본에는 syscall3 까지만 있어 syscall4 를
   새로 정의했다. 인자 제약은 syscall2, syscall3 과 같은 "r"(레지스터)를 썼다.
   syscall1 처럼 "g" 를 쓰면 인자가 esp 기준 메모리 피연산자로 잡힐 수 있는데,
   앞선 push 로 esp 가 이미 이동한 상태라 잘못된 위치를 읽게 된다.
   정리할 스택 크기는 인자 4개 + 번호 1개 = 20바이트다.
3. syscall_handler 의 인자 배열을 Arg[3] 에서 Arg[4] 로 넓혔다. Arg[3] 인 채로
   get_argument(f->esp, Arg, 4) 를 호출하면 커널 스택 범위를 넘겨 쓰게 된다.
4. fibonacci 는 재귀가 아닌 반복문으로 구현했다. 커널 스택 깊이와 실행 시간을
   함께 줄이기 위해서다. F(1) = 1, F(2) = 1 기준이어야 fibonacci(10) 이 55 가 된다.
5. 계산 자체는 커널의 userprog/syscall.c 에서 수행한다. 사용자 라이브러리는
   int $0x30 으로 요청을 넘기는 역할만 한다. 그래야 시스템 콜을 구현한 것이 된다.

### 사용자 레벨 호출부터 복귀까지의 흐름 (보고서 III.B 재사용 가능)

additional.c 의 max_of_four_int(a, b, c, d)
  → lib/user/syscall.c 의 syscall4 : 인자 4개와 번호를 역순으로 push 후 int $0x30
  → 0x30 인터럽트 → userprog/syscall.c 의 syscall_handler()
  → check_address() 로 esp 검증 → 번호 판별 → get_argument(f->esp, Arg, 4)
  → max_of_four_int() 계산 → 결과를 f->eax 에 저장
  → 인터럽트 복귀 시 eax 가 반환값이 되어 사용자 프로그램으로 전달

### IV.D 시험 및 평가 — 추가 시스템 콜

명령

```
pintos --filesys-size=2 -p ../examples/additional -a additional \
  -- -f -q run 'additional 10 20 62 40'
```

출력

```
55 62
additional: exit(0)
Exception: 0 page faults
```

55 는 열 번째 피보나치 수, 62 는 네 정수 중 최댓값이며 슬라이드 55 의 기대 출력과 일치한다. page fault 가 0 인 것으로 Arg 배열을 4칸으로 넓힌 뒤에도 커널 스택을 침범하지 않았음을 확인했다.

회귀 확인 : echo x 정상 동작 (echo: exit(0)). 시스템 콜 번호를 enum 끝에 추가한 방식이 기존 분기에 영향을 주지 않았다.

증거 파일 : docs/verify/1-1-8-additional.png

---

## 1-1-9 Prj1-1 전체 회귀 검증

### IV.D 시험 및 평가 — 최종 회귀 결과

1-1-8 완료 시점에 `make check` 와 `make grade` 를 수행했다.

```
TOTAL TESTING SCORE: 41.3%

tests/userprog/Rubric.functionality   66/108
tests/userprog/Rubric.robustness      70/ 88
tests/userprog/no-vm/Rubric            0/  1
tests/filesys/base/Rubric              0/ 30
```

1-1-7 시점 결과와 점수가 완전히 동일하다. 1-1-8 의 변경(시스템 콜 번호 추가, Arg 배열 확장)이 기존 동작에 어떤 영향도 주지 않았음을 뜻한다.

Prj1-1 채점 대상 21개는 전부 만점이다. 배점은 args 계열 3점씩, exec / wait / exit / sc-boundary 계열 5점씩, halt 3점, multi-recurse 15점이다.

남은 35개 실패는 모두 파일 시스템 관련(create, open, read, write, close, rox 계열과 tests/filesys/base/\*)으로 Prj1-2 범위다. 다만 현재 Robustness 가 70 / 88 로 높게 나오는 것은 착시가 섞여 있다. 파일 관련 시스템 콜이 아직 없어 syscall_handler 의 default 분기에서 exit(-1) 로 떨어지는데, 그 결과가 우연히 기대 출력과 맞은 항목이 있기 때문이다. create-empty 가 3 / 3 인데 create-normal 이 0 / 3 인 것이 그 예다. Prj1-2 에서 실제 구현을 넣으면 이 항목들이 일시적으로 깨질 수 있다.

증거 파일

- docs/verify/1-1-9-check.txt : make check 전체 출력
- docs/verify/1-1-9-grade.txt : make grade 결과

### git 커밋 1회차

지침서 6.1 에 따라 Prj1-1 전체 검증 통과 직후 1회차 커밋을 남겼다. 커밋 범위에 build/ 와 오브젝트 파일이 포함되지 않았음을 레포에서 확인했다.

---

## 캡처 보관 위치

- docs/verify/1-1-1-baseline.txt : 구현 전 make check 결과
- docs/verify/1-1-9-check.txt, 1-1-9-grade.txt : Prj1-1 완료 시점 결과
- docs/verify/ : 단계별 검증 화면 캡처를 1-x-y.png 형식으로 누적

## 남은 임시 코드

- 없음 (process_wait () 의 임시 대기 루프는 1-1-7 에서 제거됨)

## 아직 남기지 않은 캡처

- 1-1-2 hex_dump 스택 배치 (본 문서에 텍스트로는 기록됨)
- echo x 실행 화면
- additional 10 20 62 40 → 55 62 (보고서 V.A 필수)
- 최종 make check 결과 (제출 전 필수)

---

## 1-2-1 File Descriptor 자료구조

### II.A 구현 이유와 기대 결과

- 이유 : 현재 read 와 write 는 fd 가 0 과 1 일 때만 동작하고 나머지 번호에는 -1 을 돌려준다. 사용자 프로그램이 파일을 열고 그 결과를 번호로 다시 지목하려면, 프로세스마다 fd 번호와 `struct file *` 을 연결하는 표가 먼저 있어야 한다
- 기대 결과 : 이 단계만으로 통과하는 테스트는 없다. 1-2-2 이후의 create, open, close, read, write, seek, tell 이 올라설 토대를 만들고, 기존 21개 동작에는 영향을 주지 않는다

### II.B File Descriptor 자료구조와 선택 이유

Pintos 의 fd 는 표준 C 의 `FILE *` 에 대응하는 정수 번호이며 open 의 반환값이다. 조교 슬라이드 69 에 따르면 각 스레드가 서로 독립적인 fd 집합을 관리하고 0 은 STDIN, 1 은 STDOUT 이 선점하므로, 실제 파일에는 2번부터 배정한다.

| 후보 | 장점 | 채택하지 않은 이유 |
| --- | --- | --- |
| struct thread 안의 고정 배열 | 할당이 실패할 일이 없다 | 128칸만 잡아도 512바이트다. thread.h 원문 주석이 `struct thread` 를 1 kB 아래로 유지하고 큰 배열은 malloc 이나 palloc_get_page 로 잡으라고 명시한다 |
| 리스트 + fd 노드 | 열린 파일 수만큼만 메모리를 쓴다 | fd 로 `struct file *` 을 찾는 연산이 O(n) 이고, close 마다 노드 할당과 해제를 관리해야 한다 |
| **포인터 한 개 + 페이지 한 장 (채택)** | 구조체는 4바이트만 커지고 조회가 배열 인덱싱이라 O(1) 이다. PAL_ZERO 로 전체 칸을 한 번에 NULL 로 만들 수 있고 해제도 페이지 반납 한 번이다 | - |

채택한 형태는 `struct file **Fd_table` 한 개다. 실체는 프로세스 적재 시점에 `palloc_get_page (PAL_ZERO)` 로 잡는 4 kB 페이지이며, `PGSIZE / sizeof (struct file *)` 로 1024칸이 된다.

| 상수 | 값 | 의미 |
| --- | --- | --- |
| FD_BASE | 2 | 실제 파일에 배정되는 첫 번호, 0 과 1 은 콘솔 예약 |
| FD_MAX | 1024 | 테이블 한 페이지에 들어가는 항목 수 |

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/threads/thread.h, src/threads/thread.c, src/userprog/process.c |
| 추가 자료구조 | struct thread 의 `struct file **Fd_table` |
| 추가 상수 | FD_BASE, FD_MAX |
| 추가 include | threads/vaddr.h (PGSIZE) |
| 수정 함수 | init_thread (), start_process (), process_exit () |
| 사용한 내장 함수 | palloc_get_page (), palloc_free_page () — threads/palloc.h, file_close () — filesys/file.h |

변경 내용

1. thread.h : USERPROG 블록의 1-1-7 필드 아래에 `Fd_table` 포인터를 추가하고, fd 번호 규약을 FD_BASE, FD_MAX 상수로 분리했다. PGSIZE 를 쓰기 위해 threads/vaddr.h 를 포함했다
2. thread.c : `init_thread ()` 에서 `Fd_table` 을 NULL 로 초기화한다
3. process.c : `start_process ()` 에서 `palloc_get_page (PAL_ZERO)` 로 페이지를 잡고, 실패하면 적재 실패와 같은 경로로 종료한다. `process_exit ()` 에서 FD_BASE 부터 열린 파일을 모두 `file_close ()` 한 뒤 페이지를 반납한다

### 설계 판단

- `struct file` 을 불완전 타입인 채로 두고 filesys/file.h 를 thread.h 에 포함하지 않았다. 포인터의 크기는 타입이 불완전해도 확정되므로 헤더 의존을 늘릴 이유가 없다
- FD_MAX 를 `((int) (PGSIZE / sizeof (struct file *)))` 로 캐스팅했다. `sizeof` 결과는 부호 없는 정수라 `int fd` 와 비교할 때 빌드 옵션 `-W` 의 부호 비교 경고가 발생하기 때문이다
- 다음 fd 번호를 기억하는 카운터는 두지 않았다. 카운터만 증가시키면 close 로 비운 칸을 재사용하지 못해 fd 가 고갈된다. open 에서 FD_BASE 부터 빈 칸을 찾으면 재사용과 고갈 방지가 동시에 해결된다
- 테이블 할당 위치를 `init_thread ()` 가 아니라 `start_process ()` 로 잡았다. 커널 스레드는 파일 디스크립터를 쓰지 않는데, 모든 스레드가 4 kB 페이지를 하나씩 더 물면 메모리 압박 상황에서 손해만 커진다
- 할당 실패는 적재 실패와 동일하게 처리한다. `success = Fd_table != NULL && load (...)` 형태로 두어, 이미 검증된 1-1-7 의 실패 경로(is_loaded = false, Load_sema 해제, exec 가 -1 반환)를 그대로 재사용했다. 별도의 종료 경로를 새로 만들지 않은 것이 핵심이다
- 종료 시 파일을 닫는 책임은 `process_exit ()` 에 두었다. 정상 exit 뿐 아니라 page fault 로 인한 강제 종료도 이 함수를 거치므로, 1-1-5 에서 종료 메시지 출력 지점을 한곳에 모은 것과 같은 이유다. 닫지 않으면 inode 참조가 남아 remove 이후에도 공간이 회수되지 않는다

### IV.A Flow Chart 소재

```
프로세스 생성
  process_execute()  [userprog/process.c]
    └─ thread_create()  [threads/thread.c]
         └─ init_thread()
              └─ Fd_table = NULL
    └─ start_process()
         ├─ Fd_table = palloc_get_page(PAL_ZERO)     ← 1024칸, 전부 NULL
         └─ success = (Fd_table != NULL) && load(...)
              └─ 실패 시 is_loaded = false → 부모의 exec 가 -1

파일 사용 (1-2-2 이후)
  open("sample.txt")
    ├─ filesys_open()  [filesys/filesys.c]
    └─ FD_BASE 부터 빈 칸 탐색 → 그 인덱스를 fd 로 반환

프로세스 종료
  process_exit()
    ├─ 자식들의 Destroy_sema 해제                    (1-1-7)
    ├─ FD_BASE ~ FD_MAX 순회하며 file_close()        (1-2-1)
    ├─ palloc_free_page(Fd_table)
    └─ "이름: exit(상태)" 출력                        (1-1-5)
```

### IV.B 개발상 발생한 문제와 해결책

- `make` 출력에 경고 2건이 남아 있으나 둘 다 배포본 원본이다. `threads/init.c` 의 noreturn 함수 반환 경로 지적과 `lib/kernel/debug.c:82` 의 `__builtin_frame_address (1)` 지적으로, 이번 변경과 무관해 손대지 않았다

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 경고 증가 없음 (배포본 경고 2건 유지) |
| echo x | `echo: exit(0)`, `Console: 894 characters`, page fault 0 — 1-1-9 시점과 동일 |
| 회귀 | 지침에 따라 1-2-8 에서 일괄 수행 |

---

## 1-2-2 create, remove

### II.A 구현 이유와 기대 결과

- 이유 : 파일을 열거나 읽으려면 먼저 파일이 존재해야 한다. create 와 remove 는 파일 시스템 시스템 콜 중 fd 를 쓰지 않는 유일한 쌍이라, 파일 디스크립터 계층 없이 먼저 구현할 수 있다
- 기대 결과 : create-normal, create-empty, create-null, create-bad-ptr, create-long, create-exists, create-bound 7개 통과. remove 는 단독 테스트가 없고 tests/filesys/base/syn-remove 에서 확인된다

### II.B 시스템 콜 설명

- create : 주어진 이름으로 initial_size 바이트 크기의 빈 파일을 만들고 성공 여부를 반환한다. 파일을 만들기만 할 뿐 열지는 않으므로 fd 는 배정하지 않는다. 이미 같은 이름이 있으면 false 를 돌려준다
- remove : 주어진 이름의 파일을 지우고 성공 여부를 반환한다. 파일이 열려 있어도 삭제는 성공하며, 이미 연 프로세스는 자신의 fd 로 계속 접근할 수 있다

### 테스트가 요구하는 동작

| 테스트 | 전달 값 | 기대 결과 |
| --- | --- | --- |
| create-normal | "quux.dat", 0 | true |
| create-empty | "", 0 | false (프로세스는 정상 종료) |
| create-null | NULL | exit(-1) |
| create-bad-ptr | 0x20101234 | exit(-1) |
| create-long | 511자 이름 | false |
| create-exists | 같은 이름 재생성 | 두 번째는 false |
| create-bound | 페이지 경계에 걸친 이름 | true |

확정된 요구사항

1. 잘못된 포인터는 값을 읽기 전에 걸러야 한다 (create-null, create-bad-ptr)
2. 이름이 비었거나 너무 길면 프로세스를 죽이지 않고 false 만 반환해야 한다 (create-empty, create-long)
3. 이름이 페이지 경계를 넘어가도 끝까지 읽어야 한다 (create-bound)

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | bool create (const char \*file, unsigned initial_size), bool remove (const char \*file) |
| 수정 함수 | syscall_handler () |
| 추가 include | filesys/filesys.h |
| 사용한 내장 함수 | filesys_create (), filesys_remove () — filesys/filesys.c |

변경 내용

- SYS_CREATE, SYS_REMOVE 분기를 채우고 반환값을 f->eax 에 저장했다 (슬라이드 28)
- create 는 인자가 2개이므로 `get_argument (f->esp, Arg, 2)` 로 꺼낸다
- 두 함수 모두 이름 포인터를 1-1-7 에서 만든 check_string () 으로 먼저 검증한다

### 설계 판단

- 이름 길이 검사를 시스템 콜 계층에 두지 않았다. `dir_add ()` 가 빈 이름과 NAME_MAX 초과를 이미 걸러 false 를 돌려주므로, 같은 규칙을 두 곳에 두면 나중에 파일 시스템 쪽이 바뀔 때 어긋난다. create-empty 와 create-long 이 이 경로를 그대로 확인한다
- check_string () 을 재사용했다. exec 에서 이미 검증된 함수라 create-bad-ptr 과 create-bound 가 요구하는 페이지 경계 처리가 그대로 적용된다. 시작 주소만 보는 검사로는 create-bound 를 통과할 수 없다
- remove 에서 열린 fd 를 뒤져 닫지 않는다. 매뉴얼 3.3.5 는 삭제된 파일이라도 이미 연 참조는 유효하게 유지되어야 한다고 명시한다. 실제 공간 회수는 마지막 참조가 닫힐 때 inode 계층이 처리한다
- case 배치는 lib/syscall-nr.h 의 번호 순서가 아니라 기존 분기 뒤에 파일 관련 시스템 콜끼리 모이도록 했다. switch 는 순서와 무관하게 동작하고, 1-2 에서 추가되는 분기를 한곳에서 읽을 수 있는 편이 낫다
- 파일 시스템 접근에 대한 lock 은 아직 넣지 않는다. 1-2-7 에서 임계 구역을 한 번에 정리한다

### IV.A Flow Chart 소재

```
create("quux.dat", 0)  [사용자]
  └─ int $0x30
       └─ syscall_handler()
            ├─ check_address(f->esp, 4)
            ├─ syscall_number = SYS_CREATE
            └─ case SYS_CREATE
                 ├─ get_argument(f->esp, Arg, 2)
                 └─ create(Arg[0], Arg[1])
                      ├─ check_string(file)      ← NULL / 잘못된 주소면 exit(-1)
                      └─ filesys_create(file, initial_size)
                           ├─ free_map_allocate + inode_create
                           └─ dir_add()          ← 빈 이름 / 길이 초과면 false
                                └─ 결과를 f->eax 로 반환
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 우리 코드에서 경고 0건 |
| create-normal, create-empty, create-null, create-bad-ptr, create-long, create-exists, create-bound | 7개 모두 pass |

create-empty 는 `create(""): 0`, create-long 은 `create("x..."): 0` 을 출력하고 프로세스는 exit(0) 으로 끝났다. 길이 검사를 시스템 콜 계층에 넣지 않고 dir_add () 에 맡긴 판단이 맞았음을 확인했다.

---

## 1-2-3 open, close, filesize

### II.A 구현 이유와 기대 결과

- 이유 : 1-2-1 에서 만든 Fd_table 은 아직 빈 채로 있다. open 이 fd 를 배정하고 close 가 반납해야 파일 입출력의 진입점과 종료점이 생기며, 그 위에서 read 와 write 를 fd 기반으로 확장할 수 있다
- 기대 결과 : open-normal, open-missing, open-boundary, open-empty, open-null, open-bad-ptr, open-twice, close-normal, close-twice, close-stdin, close-stdout, close-bad-fd 12개 통과

### II.B 시스템 콜 설명

- open : 이름으로 파일을 열고 이 프로세스의 fd 번호를 배정해 반환한다. 실패하면 -1 이며, 같은 파일을 두 번 열면 서로 다른 fd 와 서로 다른 파일 위치를 갖는다
- close : fd 가 가리키는 파일을 닫고 테이블의 그 칸을 비운다. 유효하지 않은 fd 는 조용히 무시한다
- filesize : fd 가 가리키는 파일의 바이트 크기를 반환한다. 유효하지 않은 fd 이면 -1 이다

### 테스트가 요구하는 동작

| 테스트 | 전달 값 | 기대 결과 |
| --- | --- | --- |
| open-normal | "sample.txt" | fd >= 2 |
| open-missing | "no-such-file" | -1, 프로세스는 정상 종료 |
| open-empty | "" | -1 |
| open-null / open-bad-ptr | NULL, 0x20101234 | exit(-1) |
| open-boundary | 페이지 경계에 걸친 이름 | fd > 1 |
| open-twice | 같은 파일 두 번 | 서로 다른 fd 두 개 |
| close-normal | 연 파일 닫기 | exit(0) |
| close-twice | 같은 fd 두 번 닫기 | 조용한 실패 또는 exit(-1) |
| close-stdin / close-stdout | close(0), close(1) | 조용한 실패 또는 exit(-1) |
| close-bad-fd | close(0x20101234) | 조용한 실패 또는 exit(-1) |

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | get_file () (static), open (), close (), filesize () |
| 수정 함수 | syscall_handler () |
| 추가 include | filesys/file.h |
| 사용한 내장 함수 | filesys_open () — filesys/filesys.c, file_close (), file_length () — filesys/file.c |

변경 내용

- SYS_OPEN, SYS_FILESIZE, SYS_CLOSE 분기를 채웠다. close 는 반환값이 없어 f->eax 를 건드리지 않는다
- open 은 filesys_open () 이 준 포인터를 FD_BASE 부터 찾은 첫 빈 칸에 넣고 그 인덱스를 반환한다
- fd 를 struct file * 로 바꾸는 검증을 get_file () 하나로 모았다

### 설계 판단

- fd 검증을 get_file () 한 곳에 모았다. 범위 검사(FD_BASE 이상 FD_MAX 미만), 테이블 존재 검사, 빈 칸 검사를 매번 되풀이하면 한 군데만 빠뜨려도 커널 메모리를 읽는 버그가 된다. close-bad-fd 가 넘기는 0x20101234 는 위쪽 범위 검사가 없으면 그대로 인덱싱된다
- 잘못된 fd 에 대해 조용한 실패를 택했다. close-twice, close-stdin, close-stdout, close-bad-fd 는 조용한 실패와 exit(-1) 을 모두 정답으로 인정하지만, 프로세스를 죽이지 않는 쪽이 이후 테스트에서 부작용이 적다. 특히 자식 프로세스가 닫기 실수로 죽으면 부모의 wait 결과까지 달라진다
- close 에서 file_close () 뒤에 칸을 NULL 로 되돌린다. 이것이 close-twice 의 핵심이다. 비우지 않으면 두 번째 close 가 이미 해제된 포인터를 file_close () 에 다시 넘겨 커널이 무너진다
- 테이블이 가득 찬 경우 open 은 이미 연 파일을 file_close () 한 뒤 -1 을 반환한다. 그냥 -1 만 돌려주면 어떤 fd 로도 닿을 수 없는 열린 파일이 남아 누수가 된다
- 없는 파일과 빈 이름은 프로세스를 죽이지 않고 -1 만 반환한다. filesys_open () 이 NULL 을 돌려주는 정상적인 실패이며, open-missing 과 open-empty 가 이를 확인한다. 반면 잘못된 포인터는 사용자 메모리 접근 위반이라 check_string () 이 exit(-1) 로 처리한다

### IV.A Flow Chart 소재

```
open("sample.txt")  [사용자]
  └─ syscall_handler() → case SYS_OPEN
       └─ open(Arg[0])
            ├─ check_string(file)              ← NULL / 잘못된 주소면 exit(-1)
            ├─ filesys_open(file)              ← 없거나 빈 이름이면 NULL → -1
            └─ Fd_table[FD_BASE ..] 첫 빈 칸에 저장 → 그 인덱스 반환
                 └─ 빈 칸이 없으면 file_close() 후 -1

close(fd)
  └─ get_file(fd)                              ← 범위 밖 / 빈 칸이면 NULL
       ├─ NULL 이면 조용히 반환
       └─ file_close() 후 Fd_table[fd] = NULL
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 경고 0건 |
| open-normal, open-missing, open-boundary, open-empty, open-null, open-bad-ptr, open-twice | 7개 모두 pass |
| close-normal, close-twice, close-stdin, close-stdout, close-bad-fd | 5개 모두 pass |

close-twice 가 커널 패닉 없이 통과한 것은 close 후 테이블 칸을 NULL 로 되돌리는 처리가 동작한다는 뜻이다.

---

## 1-2-4 read, write 의 파일 디스크립터 확장

### II.A 구현 이유와 기대 결과

- 이유 : 1-1-6 의 read 와 write 는 fd 0 과 1 만 처리하고 나머지는 -1 을 돌려준다. 1-2-3 에서 open 이 fd 를 배정하게 되었으므로, 그 fd 로 실제 파일을 읽고 쓸 수 있어야 파일 입출력이 완성된다
- 기대 결과 : read-normal, read-zero, read-boundary, read-stdout, read-bad-fd, write-normal, write-zero, write-boundary, write-stdin, write-bad-fd, multi-child-fd 통과

### II.B 시스템 콜 설명

- read : fd 가 0 이면 키보드에서 한 글자씩 읽고, 2 이상이면 Fd_table 에서 찾은 파일에서 size 바이트를 읽는다. 실제로 읽은 바이트 수를 반환한다
- write : fd 가 1 이면 콘솔에 출력하고, 2 이상이면 해당 파일에 기록한다. 실제로 쓴 바이트 수를 반환한다

### 테스트가 요구하는 동작

| 테스트 | 전달 값 | 기대 결과 |
| --- | --- | --- |
| read-normal | sample.txt 전체 읽기 | 내용이 정확히 일치 |
| read-zero | size 0 | 0 반환, 버퍼 변경 없음 |
| read-stdout | fd 1 로 읽기 | 조용한 실패 또는 exit(-1) |
| read-bad-fd | 0x20101234, 5, 1234, -1, -1024, INT_MIN, INT_MAX | 조용한 실패 또는 exit(-1) |
| write-normal | 연 파일에 sample 기록 | 요청한 바이트 수와 동일한 반환값 |
| write-zero | size 0 | 0 반환 |
| write-stdin | fd 0 으로 쓰기 | 조용한 실패 또는 exit(-1) |
| write-bad-fd | 잘못된 fd 7종 | 조용한 실패 또는 exit(-1) |
| multi-child-fd | 자식이 부모의 fd 를 close 시도 | 자식의 close 는 무효, 부모는 계속 사용 가능 |

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c |
| 수정 함수 | read (), write () |
| 추가 선언 | get_file () 전방 선언 |
| 사용한 내장 함수 | file_read (), file_write () — filesys/file.c |

변경 내용

- read 의 fd == 0 경로와 write 의 fd == 1 경로는 그대로 두고, 그 아래에 get_file () 로 얻은 struct file \* 을 사용하는 분기를 추가했다
- 기존의 마지막 `return -1;` 두 줄이 이 분기로 대체되었다. 유효하지 않은 fd 이면 여전히 -1 이 반환된다
- get_file () 정의가 read, write 보다 아래에 있어 전방 선언을 추가했다. 정의를 위로 옮기면 1-2-3 에서 확인한 배치가 흔들리므로 선언만 앞으로 뺐다

### 설계 판단

- 표준 입출력 분기를 먼저 검사하고 파일 분기를 뒤에 두었다. fd 0 과 1 은 Fd_table 에서 항상 NULL 이므로 순서를 바꾸면 콘솔 입출력이 -1 로 떨어진다
- 잘못된 fd 에 대해 1-2-3 의 close 와 같은 조용한 실패를 유지했다. read-bad-fd 와 write-bad-fd 는 한 프로세스에서 7번을 연달아 호출하므로, 첫 호출에서 죽이면 나머지 경로를 확인할 수 없다
- 자식 프로세스가 부모의 fd 를 닫지 못하는 것은 별도 처리 없이 자료구조에서 보장된다. Fd_table 이 스레드마다 따로 있고 exec 이 이를 복제하지 않으므로, 자식의 Fd_table[handle] 은 NULL 이고 close 는 아무 일도 하지 않는다. multi-child-fd 가 요구하는 동작이 곧 1-2-1 의 설계 결과다
- 실행 파일 쓰기 금지는 여기서 처리하지 않았다. write 안에서 파일 이름을 비교하는 방식은 확장성이 없고, 1-2-6 에서 file_deny_write () 로 파일 쪽에 표시하면 file_write () 가 알아서 0 을 돌려준다

### IV.A Flow Chart 소재

```
read(fd, buffer, size)
  ├─ check_address(buffer, size)        ← size 0 이면 즉시 통과
  ├─ fd == 0 ?  → input_getc() 반복 → size 반환
  └─ get_file(fd)
       ├─ NULL → -1                      (fd 1, 범위 밖, 빈 칸)
       └─ file_read(File, buffer, size) → 읽은 바이트 수

write(fd, buffer, size)
  ├─ check_address(buffer, size)
  ├─ fd == 1 ?  → putbuf() → size 반환
  └─ get_file(fd)
       ├─ NULL → -1                      (fd 0, 범위 밖, 빈 칸)
       └─ file_write(File, buffer, size) → 쓴 바이트 수
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 경고 0건 |
| read-normal, read-zero, read-boundary, read-stdout, read-bad-fd | 5개 모두 pass |
| write-normal, write-zero, write-boundary, write-stdin, write-bad-fd | 5개 모두 pass |
| multi-child-fd | pass |

multi-child-fd 통과는 자식이 부모의 fd 를 닫지 못한다는 뜻이며, 1-2-1 에서 테이블을 프로세스별로 둔 설계가 그대로 요구사항을 만족시켰다.

---

## 1-2-5 seek, tell

### II.A 구현 이유와 기대 결과

- 이유 : 지금까지의 read 와 write 는 파일을 앞에서부터 순서대로만 다룬다. 파일의 임의 위치를 지목할 수 있어야 파일 시스템 시스템 콜 13개가 모두 채워진다
- 기대 결과 : tests/userprog 에는 seek, tell 단독 테스트가 없다. tests/filesys/base 의 sm-random, lg-random 이 임의 순서로 블록을 쓰고 읽으며 seek 를 직접 사용한다

### II.B 시스템 콜 설명

- seek : fd 가 가리키는 파일의 다음 읽기 / 쓰기 위치를 파일 시작에서 position 바이트 지점으로 옮긴다. 반환값이 없다
- tell : fd 가 가리키는 파일의 현재 위치를 파일 시작으로부터의 바이트 수로 반환한다

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h |
| 추가 함수 | void seek (int fd, unsigned position), unsigned tell (int fd) |
| 수정 함수 | syscall_handler () |
| 사용한 내장 함수 | file_seek (), file_tell () — filesys/file.c |

변경 내용

- SYS_SEEK (인자 2개, 반환값 없음), SYS_TELL (인자 1개, f->eax 에 저장) 분기를 추가했다
- 두 함수 모두 1-2-3 의 get_file () 로 fd 를 검증한다

이로써 Prj1-2 가 요구하는 파일 관련 시스템 콜 9개(create, remove, open, filesize, read, write, seek, tell, close)가 모두 채워졌다.

### 설계 판단

- 위치 정보를 Fd_table 이나 별도 구조체에 따로 두지 않았다. 위치는 struct file 안의 pos 필드가 이미 관리한다. 같은 파일을 두 번 열면 struct file 이 두 개 생기므로 위치도 자연스럽게 분리되며, 이것이 open-twice 가 요구하는 독립성과 같은 성질이다
- 파일 끝을 넘어선 seek 을 오류로 막지 않았다. 매뉴얼은 이를 정상 동작으로 규정하고, 그 자리에서 읽으면 0 바이트가 반환된다. 시스템 콜 계층에서 범위를 제한하면 매뉴얼과 어긋난다
- tell 이 실패할 때 -1 을 반환하도록 했다. 반환형이 unsigned 라 0xffffffff 로 전달되지만, 매뉴얼이 실패 시 반환값을 규정하지 않는 이상 다른 시스템 콜과 같은 실패 표시를 쓰는 편이 일관적이다. 파일 크기가 0xffffffff 에 이를 일은 없다
- seek 은 잘못된 fd 에 대해 조용히 무시한다. 1-2-3 의 close, 1-2-4 의 read / write 와 같은 규칙이며, 실패 처리 방식이 시스템 콜마다 달라지지 않도록 맞췄다

### IV.A Flow Chart 소재

```
seek(fd, position)
  └─ get_file(fd)
       ├─ NULL → 조용히 반환
       └─ file_seek(File, position)   → struct file 의 pos 갱신

tell(fd)
  └─ get_file(fd)
       ├─ NULL → -1
       └─ file_tell(File)             → 현재 pos 반환
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 우리 코드에서 경고 0건 |
| tests/filesys/base : sm-create, sm-full, sm-random, sm-seq-block, sm-seq-random | 5개 모두 pass |

sm-random 과 sm-seq-random 은 블록을 무작위 순서로 seek 해가며 쓴 뒤 다시 읽어 내용을 대조한다. 통과했다는 것은 위치 이동이 정확히 반영된다는 뜻이다.

---

## 1-2-6 실행 파일 쓰기 금지 (Denying Writes to Executables)

### II.A 구현 이유와 기대 결과

- 이유 : 프로세스가 도는 동안 그 실행 파일이 수정되면, 아직 적재하지 않은 부분을 읽을 때 내용이 뒤바뀌어 있을 수 있다. 매뉴얼과 조교 슬라이드 72 는 실행 중인 파일에 대한 쓰기를 커널이 막도록 요구한다
- 기대 결과 : rox-simple, rox-child, rox-multichild 3개 통과

### II.B 구현 방식

Pintos 는 이 목적을 위한 API 를 이미 제공한다.

| 함수 | 동작 |
| --- | --- |
| file_deny_write (struct file \*) | 해당 inode 의 deny_write_cnt 를 올린다. 이후 그 inode 에 대한 쓰기는 0 바이트만 기록된다 |
| file_allow_write (struct file \*) | 카운트를 되돌린다. file_close () 가 내부에서 호출한다 |

핵심은 **파일을 닫으면 금지가 풀린다**는 점이다. 따라서 실행 파일을 열어 둔 채로 프로세스가 끝날 때까지 붙잡고 있어야 한다.

### 테스트가 요구하는 동작

| 테스트 | 상황 | 기대 결과 |
| --- | --- | --- |
| rox-simple | 자기 실행 파일을 열어 쓰기 시도 | write 가 0 반환 |
| rox-child | 자식이 부모의 실행 파일에 쓰기 시도 | write 가 0 반환 |
| rox-multichild | 여러 단계의 자식이 같은 시도 | 모두 0 반환 |

금지 상태가 inode 단위로 걸리기 때문에, 다른 프로세스가 같은 파일을 새로 열어 얻은 fd 로 써도 막힌다. rox-child 와 rox-multichild 가 이 성질을 확인한다.

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/threads/thread.h, src/threads/thread.c, src/userprog/process.c |
| 추가 자료구조 | struct thread 의 `struct file *Exec_file` |
| 수정 함수 | init_thread (), load (), process_exit () |
| 사용한 내장 함수 | file_deny_write (), file_close () — filesys/file.c |

변경 내용

1. thread.h : 실행 중인 파일을 붙잡아 둘 `Exec_file` 필드를 추가했다
2. thread.c : `init_thread ()` 에서 NULL 로 초기화한다
3. process.c
   - `load ()` : 실행 파일을 연 직후 `file_deny_write ()` 를 부르고 `Exec_file` 에 보관한다
   - `load ()` 의 done 라벨 : 무조건 부르던 `file_close ()` 를 적재 실패 시에만 부르도록 바꾸고, 그때 `Exec_file` 도 NULL 로 되돌린다
   - `process_exit ()` : `Exec_file` 이 있으면 닫는다

### 설계 판단

- 쓰기 금지를 write 시스템 콜 쪽에서 처리하지 않았다. 실행 파일 이름을 비교하거나 fd 를 검사하는 방식은 같은 파일을 다른 이름으로 열면 뚫리고, 자식 프로세스가 부모의 실행 파일에 쓰는 rox-child 를 막지 못한다. inode 단위로 표시하는 Pintos 의 기존 장치를 쓰면 두 경우가 한 번에 해결된다
- done 라벨의 `file_close ()` 를 실패 경로로 한정한 것이 이 단계의 핵심이다. 이 한 줄을 그대로 두면 방금 건 금지가 즉시 풀려 rox 계열이 전부 깨진다. 반대로 조건 없이 지우면 적재에 실패한 파일이 닫히지 않아 누수가 된다
- 실패 시 `Exec_file` 을 NULL 로 되돌린다. 여기서 닫고 필드에도 남겨 두면 `process_exit ()` 이 같은 포인터를 한 번 더 닫아 이중 해제가 된다
- 파일을 닫는 책임을 `process_exit ()` 에 모았다. 1-2-1 의 Fd_table 반납, 1-1-5 의 종료 메시지 출력과 같은 자리이며, 정상 종료와 page fault 강제 종료가 모두 이 함수를 지난다
- `file_allow_write ()` 를 직접 부르지 않는다. `file_close ()` 가 내부에서 호출하므로 중복해서 부르면 카운트가 어긋난다

### IV.A Flow Chart 소재

```
load()
  ├─ filesys_open(Prog_name)
  ├─ file_deny_write(file)              ← inode 의 deny_write_cnt 증가
  ├─ Exec_file = file                    ← 프로세스가 끝날 때까지 붙잡음
  ├─ ... ELF 헤더 검증, 세그먼트 적재, 스택 구성 ...
  └─ done:
       └─ 실패한 경우에만 Exec_file = NULL, file_close(file)

다른 프로세스의 write(fd, ...)
  └─ file_write() → inode_write_at()
       └─ deny_write_cnt > 0 이면 0 바이트 반환

process_exit()
  ├─ 자식들의 Destroy_sema 해제           (1-1-7)
  ├─ 열린 파일 정리, Fd_table 반납         (1-2-1)
  ├─ file_close(Exec_file)                (1-2-6) → file_allow_write() 자동 호출
  └─ "이름: exit(상태)" 출력               (1-1-5)
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 우리 코드에서 경고 0건 |
| rox-simple, rox-child, rox-multichild | 3개 모두 pass |
| 회귀 확인 : multi-recurse, multi-child-fd | 2개 모두 pass |

실행 파일을 프로세스 종료까지 붙잡는 구조로 바뀌면서 파일 해제 경로가 달라졌으므로 회귀 항목을 함께 돌렸다. 이중 해제나 누수로 인한 패닉은 발생하지 않았다.

---

## 1-2-7 파일 시스템 동기화

### II.A 구현 이유와 기대 결과

- 이유 : Pintos 의 기본 파일 시스템에는 내부 동기화가 전혀 없다(매뉴얼 3.1.2). 여러 프로세스가 동시에 파일을 만들거나 읽고 쓰면 free map, 디렉터리, inode 가 중간 상태에서 갱신되어 깨진다. 지금까지 구현한 시스템 콜은 모두 이 계층을 직접 부른다
- 기대 결과 : tests/filesys/base 의 syn-read, syn-write, syn-remove 통과. 기존 통과 항목은 그대로 유지

### II.B 동기화 방식

전역 락 하나(`Filesys_lock`)로 파일 시스템 계층 진입을 직렬화한다. 실체는 syscall.c 에 두고 `syscall_init ()` 에서 초기화하며, process.c 에서도 쓸 수 있도록 syscall.h 에 extern 으로 공개한다.

락으로 감싼 구간

| 위치 | 감싼 호출 |
| --- | --- |
| create, remove | filesys_create (), filesys_remove () |
| open | filesys_open (), 테이블이 가득 찼을 때의 file_close () |
| read, write | file_read (), file_write () |
| filesize, seek, tell, close | file_length (), file_seek (), file_tell (), file_close () |
| load () | 함수 전체 (filesys_open () 과 세그먼트 적재의 file_read ()) |
| process_exit () | 열린 파일 닫기 반복문, 실행 파일 닫기 |

감싸지 않은 구간

| 위치 | 이유 |
| --- | --- |
| check_address (), check_string () | 검증 실패 시 exit(-1) 로 빠져나간다. 락을 쥔 채 나가면 아무도 풀 수 없다 |
| read 의 fd == 0, write 의 fd == 1 | 파일 시스템과 무관하다. 특히 input_getc () 는 입력을 기다리는 blocking 호출이라, 감싸면 그동안 모든 프로세스의 파일 접근이 멈춘다 |
| Fd_table 탐색과 페이지 반납 | 프로세스마다 따로 있는 자료라 경쟁이 없다 |

### III.B 개발 방법

| 구분 | 내용 |
| --- | --- |
| 수정 소스코드 | src/userprog/syscall.c, src/userprog/syscall.h, src/userprog/process.c |
| 추가 자료구조 | struct lock Filesys_lock (전역) |
| 수정 함수 | syscall_init (), create (), remove (), open (), read (), write (), filesize (), seek (), tell (), close (), load (), process_exit () |
| 추가 include | userprog/syscall.h (process.c 에서) |
| 사용한 내장 함수 | lock_init (), lock_acquire (), lock_release () — threads/synch.c |

### 설계 판단

- 락을 파일마다 두지 않고 전역 하나로 했다. Pintos 의 기본 파일 시스템은 파일 하나를 건드릴 때도 free map 과 디렉터리라는 공유 자료를 함께 수정하므로, 파일 단위로 쪼개도 그 공유 부분은 결국 다시 보호해야 한다. Project 3 에서 파일 시스템 자체를 개선할 때 잘게 나누는 것이 순서다
- 락 획득 지점을 사용자 포인터 검증 뒤로 두었다. 이 순서가 뒤바뀌면 잘못된 포인터를 넘긴 프로세스가 락을 쥔 채 exit(-1) 로 죽고, 그 뒤로 어떤 프로세스도 파일에 접근하지 못해 시스템 전체가 멈춘다. 시스템 콜 구현에서 가장 조심한 부분이다
- load () 는 함수 전체를 감쌌다. 시스템 콜 쪽만 잠그면 exec 로 프로세스가 뜨는 도중의 파일 접근이 보호되지 않는다. 첫 `goto done` 보다 앞에서 락을 잡아, 잡지 않은 락을 푸는 경우가 생기지 않게 했다
- load () 가 실패하면 done 에서 락을 먼저 풀고 반환한다. 실패 시 start_process () 가 thread_exit () 을 부르고 그 안의 process_exit () 이 같은 락을 다시 잡으므로, 풀지 않고 나가면 자기 자신을 기다리는 교착이 된다
- read 의 키보드 입력 경로를 락 밖에 둔 것도 같은 성격의 판단이다. blocking 호출을 락 안에 넣으면 한 프로세스가 키 입력을 기다리는 동안 나머지 전부가 멈춘다

### IV.A Flow Chart 소재

```
시스템 콜 진입
  ├─ check_address / check_string        ← 락 밖 (실패 시 exit(-1))
  ├─ get_file(fd)                         ← 락 밖 (프로세스 자신의 테이블)
  ├─ lock_acquire(&Filesys_lock)
  │    └─ filesys_* / file_* 호출          ← 임계 구역
  ├─ lock_release(&Filesys_lock)
  └─ 결과를 f->eax 로 반환

exec 경로
  start_process() → load()
    ├─ lock_acquire(&Filesys_lock)        ← 첫 goto done 보다 앞
    ├─ filesys_open, file_read, file_deny_write
    └─ done: (실패면 file_close) → lock_release → 반환
```

### 검증 기록

| 항목 | 결과 |
| --- | --- |
| 빌드 | 우리 코드에서 경고 0건 |
| tests/filesys/base : syn-read, syn-write, syn-remove | 3개 모두 pass |
| 회귀 확인 : multi-recurse, multi-child-fd, rox-child | 3개 모두 pass |

syn-read 는 자식 10개가 같은 파일을 동시에 읽는다. 타임아웃 없이 통과했으므로 교착이 없고, 내용 대조도 통과했으므로 감싸지 못한 임계 구역이 남아 있지 않다. rox-child 통과는 exec 경로에 락을 넣고도 적재가 정상임을 보여 준다.

---

## 1-2-8 전체 회귀 검증

### 최종 결과

```
TOTAL TESTING SCORE: 100.0%
ALL TESTED PASSED -- PERFECT SCORE
```

| 테스트 세트 | 점수 | 비중 |
| --- | --- | --- |
| tests/userprog/Rubric.functionality | 108 / 108 | 35.0% / 35.0% |
| tests/userprog/Rubric.robustness | 88 / 88 | 25.0% / 25.0% |
| tests/userprog/no-vm/Rubric | 1 / 1 | 10.0% / 10.0% |
| tests/filesys/base/Rubric | 30 / 30 | 30.0% / 30.0% |
| **합계** | **76 / 76 항목** | **100.0%** |

1-1-9 시점의 41.3% 에서 100.0% 로 올라갔다. 늘어난 58.7%p 는 전부 Prj1-2 범위(파일 시스템 콜, 실행 파일 보호, 동기화)에서 나왔다.

### 추가 구현 확인

```
Executing 'additional 10 20 62 40':
55 62
additional: exit(0)
Exception: 0 page faults
```

fibonacci (10) = 55, max_of_four_int (10, 20, 62, 40) = 62 로 슬라이드 55 의 예시와 일치한다.

### 단계별 검증 이력

| 단계 | 내용 | 검증 대상 | 결과 |
| --- | --- | --- | --- |
| 1-2-1 | File Descriptor 자료구조 | echo, multi-recurse | 통과 |
| 1-2-2 | create, remove | create 계열 7개 | 통과 |
| 1-2-3 | open, close, filesize | open / close 계열 12개 | 통과 |
| 1-2-4 | fd 기반 read, write | read / write 계열 10개, multi-child-fd | 통과 |
| 1-2-5 | seek, tell | filesys/base sm 계열 5개 | 통과 |
| 1-2-6 | 실행 파일 쓰기 금지 | rox 3개 + 회귀 2개 | 통과 |
| 1-2-7 | 파일 시스템 동기화 | syn 계열 3개 + 회귀 3개 | 통과 |
| 1-2-8 | 전체 회귀 | 76개 전체 | 100.0% |

### 눈여겨볼 결과

- **multi-oom (no-vm) 통과** : 1-2-1 에서 프로세스마다 파일 디스크립터 테이블용으로 4 kB 페이지를 하나씩 더 쓰기로 했기 때문에, 메모리를 고갈시키는 이 테스트가 가장 걱정되는 항목이었다. 통과했다는 것은 페이지 한 장의 추가 비용이 요구 재귀 깊이에 영향을 주지 않았고, 종료 시 반납도 빠짐없이 이루어졌다는 뜻이다
- **lg 계열 전부 통과** : 큰 파일에 대한 임의 접근(lg-random, lg-seq-random)까지 통과해 seek 구현이 파일 크기와 무관하게 동작함을 확인했다
- **경고 2건 유지** : `threads/init.c` 의 noreturn 반환 경로와 `lib/kernel/debug.c:82` 의 `__builtin_frame_address (1)` 로, 둘 다 배포본 원본이며 이번 프로젝트에서 손대지 않았다

### IV.B 개발 과정에서 조심한 지점 (종합)

1. **fd 배정에 카운터를 쓰지 않았다** — close 로 비운 칸을 재사용하지 못해 fd 가 고갈되는 문제를 피했다
2. **close 후 테이블 칸을 NULL 로 되돌린다** — 두 번째 close 가 해제된 포인터를 다시 넘기는 것을 막는다 (close-twice)
3. **load () 의 done 라벨에서 file_close () 를 실패 시로 한정** — file_close () 가 내부에서 file_allow_write () 를 부르므로, 조건 없이 두면 실행 파일 쓰기 금지가 즉시 풀린다 (rox 계열)
4. **락은 사용자 포인터 검증을 마친 뒤에 잡는다** — 검증 실패는 exit(-1) 로 빠져나가므로, 락을 쥔 채 죽으면 시스템 전체가 멈춘다
5. **load () 는 done 에서 락을 풀고 반환한다** — 적재 실패 시 process_exit () 이 같은 락을 다시 잡으므로, 풀지 않으면 자기 자신을 기다리는 교착이 된다
6. **blocking 호출은 임계 구역 밖에 둔다** — input_getc () 를 락 안에 넣으면 한 프로세스가 키 입력을 기다리는 동안 나머지 전부가 멈춘다