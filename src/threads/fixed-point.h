#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* [2-3-1] BSD Scheduler : 17.14 고정소수점 연산
   목적 : 부동소수점 없이 recent_cpu, load_avg 같은 실수를 계산한다
   입력 : x, y 는 고정소수점 값(int), n 은 정수
   출력 : 고정소수점 값 또는 정수
   참고 : 조교 슬라이드 41, Pintos manual B.6
   주의 : 부호 1비트, 정수부 17비트, 소수부 14비트,
          곱셈과 고정소수점끼리의 나눗셈은 int64_t 로 넓혀 넘침을 막는다,
          fp_to_int_up 은 슬라이드 38 의 thread_get_recent_cpu () 올림용이다 */
#include <stdint.h>

#define FP_Q 14
#define FP_F (1 << FP_Q)

/* [2-3-1] fp_from_int : 정수 n 을 고정소수점으로 변환 */
static inline int
fp_from_int (int n)
{
  return n * FP_F;
}

/* [2-3-1] fp_to_int_zero : 고정소수점 x 를 0 쪽으로 버려 정수로 변환 */
static inline int
fp_to_int_zero (int x)
{
  return x / FP_F;
}

/* [2-3-1] fp_to_int_nearest : 고정소수점 x 를 가장 가까운 정수로 반올림 */
static inline int
fp_to_int_nearest (int x)
{
  if (x >= 0)
    return (x + FP_F / 2) / FP_F;
  else
    return (x - FP_F / 2) / FP_F;
}

/* [2-3-1] fp_to_int_up : 고정소수점 x 를 올림하여 정수로 변환 */
static inline int
fp_to_int_up (int x)
{
  if (x >= 0)
    return (x + FP_F - 1) / FP_F;
  else
    return x / FP_F;
}

/* [2-3-1] fp_add : 고정소수점 두 값의 합 */
static inline int
fp_add (int x, int y)
{
  return x + y;
}

/* [2-3-1] fp_sub : 고정소수점 두 값의 차 */
static inline int
fp_sub (int x, int y)
{
  return x - y;
}

/* [2-3-1] fp_add_int : 고정소수점 x 에 정수 n 을 더함 */
static inline int
fp_add_int (int x, int n)
{
  return x + n * FP_F;
}

/* [2-3-1] fp_sub_int : 고정소수점 x 에서 정수 n 을 뺌 */
static inline int
fp_sub_int (int x, int n)
{
  return x - n * FP_F;
}

/* [2-3-1] fp_mul : 고정소수점 두 값의 곱 */
static inline int
fp_mul (int x, int y)
{
  return ((int64_t) x) * y / FP_F;
}

/* [2-3-1] fp_mul_int : 고정소수점 x 에 정수 n 을 곱함 */
static inline int
fp_mul_int (int x, int n)
{
  return x * n;
}

/* [2-3-1] fp_div : 고정소수점 x 를 고정소수점 y 로 나눔 */
static inline int
fp_div (int x, int y)
{
  return ((int64_t) x) * FP_F / y;
}

/* [2-3-1] fp_div_int : 고정소수점 x 를 정수 n 으로 나눔 */
static inline int
fp_div_int (int x, int n)
{
  return x / n;
}

#endif /* threads/fixed-point.h */
