/* [1-1-8] 추가 시스템 콜 검증용 사용자 프로그램
   목적 : fibonacci 와 max_of_four_int 시스템 콜을 실제로 호출해
          결과를 한 줄로 출력한다
   입력 : 명령행 인자 4개 (예 : additional 10 20 62 40)
   출력 : "fibonacci(첫 번째 인자) max_of_four_int(네 인자)" 형태의 한 줄
          예시 입력에 대한 기대 출력은 55 62
   참고 : proj1 슬라이드 55 - 실행 파일 이름은 반드시 additional 이어야 하고
          사용법은 ./additional [num 1] [num 2] [num 3] [num 4] 다
   주의 : 문자열을 정수로 바꾸는 atoi() 는 lib/stdlib.h 에 선언되어 있다
          argv[0] 은 프로그램 이름이므로 실제 숫자는 argv[1] 부터다
          인자 개수가 맞지 않으면 examples/lineup.c 와 같은 방식으로
          별도 출력 없이 exit (1) 로 끝낸다 */

#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int
main (int argc, char *argv[])
{
  int Num[4];
  int i;

  if (argc != 5)
    exit (1);

  for (i = 0; i < 4; i++)
    Num[i] = atoi (argv[i + 1]);

  printf ("%d %d\n", fibonacci (Num[0]),
          max_of_four_int (Num[0], Num[1], Num[2], Num[3]));

  return EXIT_SUCCESS;
}