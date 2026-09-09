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