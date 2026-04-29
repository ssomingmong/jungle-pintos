# 우선순위 기부 공부

정리 기준: Gemini 공유 대화에서 우선순위 기부 관련 내용만 먼저 추려서 재구성

## 공부 순서

우선순위 기부는 바로 들어가기보다 아래 순서로 보는 게 덜 헷갈린다.

1. 우선순위 공부
2. 우선순위 구현
3. 우선순위 기부 공부
4. 우선순위 기부 구현

이유:
- 기본 우선순위 스케줄링과 기부를 한 번에 섞으면 mental overload가 온다.
- 기본 우선순위 단계에서는 `ready_list` 정렬, 선점, `thread_set_priority()` 같은 기본 동작을 먼저 잡으면 된다.
- 그 다음에야 `base priority`, `effective priority`, `waiting lock`, `donors` 같은 기부용 상태를 확장하는 게 훨씬 쉽다.

## 왜 우선순위 기부가 필요한가

핵심 문제는 `priority inversion`이다.

예시:
- Low 우선순위 스레드 `L`이 락을 들고 있다.
- High 우선순위 스레드 `H`가 그 락을 원하지만 못 얻어서 block 된다.
- 이 상태에서 Medium 우선순위 스레드 `M`이 runnable이면, `L`보다 우선순위가 높아서 계속 CPU를 가져갈 수 있다.
- 그러면 정작 락을 풀어줘야 하는 `L`이 실행되지 못하고, 가장 급한 `H`가 오래 기다리는 이상한 상황이 생긴다.

우선순위 기부는 이 문제를 막기 위해, 락을 쥔 스레드의 **유효 우선순위**를 일시적으로 올려주는 장치다.

## 핵심 개념

### 1. Base priority와 Effective priority를 분리해야 한다

- `base priority`: 스레드가 원래 가지고 있던 우선순위
- `effective priority`: 기부까지 반영된 현재 실제 우선순위

즉, 스레드에는 "원래 점수"와 "현재 적용 점수"가 따로 있어야 한다.

### 2. 기부자는 자기 우선순위를 잃지 않는다

중요한 포인트:
- 우선순위는 통장에서 꺼내 주는 돈이 아니라, 스레드가 가진 원래 등급에 가깝다.
- `H(40)`가 `L(20)`에게 기부했다고 해서 `H` 자신의 우선순위가 `20`으로 깎이면 안 된다.

왜 안 되나:
- 잠든 waiter들도 결국 "누가 더 높은 우선순위인가" 기준으로 줄을 서 있어야 한다.
- 만약 `H`의 우선순위를 깎아버리면, 나중에 `M(32)`가 와서 `H`보다 앞질러 버릴 수 있다.
- 그러면 우선순위 역전을 해결하려다 오히려 역전을 다시 만드는 꼴이 된다.

정리:
- 기부자는 원래 우선순위를 유지한다.
- 수혜자는 그 우선순위를 반영한 더 높은 `effective priority`를 잠시 사용한다.

### 3. 유효 우선순위는 최대값으로 계산한다

기부가 여러 개 들어오면 보통 다음처럼 생각하면 된다.

`effective priority = max(base priority, 모든 donor priority)`

예시:
- `L`의 base priority = `20`
- `H1`이 `40` 기부
- `H2`가 `35` 기부

그러면:
- 현재 `L`의 effective priority = `40`
- `H1` 관련 기부가 사라지면 `35`로 재계산
- 모든 기부가 사라지면 다시 `20`으로 복귀

즉, "더해지는 것"보다 "최댓값으로 다시 계산하는 것"에 가깝게 이해하는 편이 안전하다.

## Multiple Donation

다중 기부에서는 한 스레드가 여러 donor에게 동시에 우선순위를 받을 수 있다.

예시 흐름:
1. `L(20)`이 `Lock A`, `Lock B`를 들고 있다.
2. `H1(40)`가 `Lock A`를 기다리면 `L`의 effective priority는 `40`이 된다.
3. `H2(35)`가 `Lock B`를 기다려도 effective priority는 여전히 `40`이다.
4. `L`이 `Lock A`를 풀어서 `H1` 관련 기부가 사라지면, 남은 donor를 기준으로 `35`로 다시 계산한다.
5. `Lock B`도 풀면 모든 기부가 사라져 `20`으로 돌아간다.

포인트:
- 기부는 누적 합이 아니라 "현재 남아 있는 donor들 중 최대 priority"를 기준으로 다시 계산해야 한다.
- 락 하나를 반납할 때마다, 그 락 때문에 걸려 있던 donation만 제거하고 전체를 재평가해야 한다.

## Nested Donation

중첩 기부는 기부가 한 단계 더 전파되는 경우다.

예시:
- `L`이 어떤 락을 들고 있다.
- `M`이 그 락을 기다리면서 `L`에게 기부한다.
- 그런데 `L`이 또 다른 락을 기다리고 있고, 그 락을 `X`가 쥐고 있다면
- `M`의 높은 우선순위가 `L`을 거쳐 `X`까지 전달될 수 있다.

즉, "락을 기다리는 사슬"을 따라 donation이 전파될 수 있다.

공부 포인트:
- nested donation은 한 번만 올리는 문제가 아니라, `waiting lock`을 따라 holder 쪽으로 계속 전달하는 구조로 이해해야 한다.
- 그래서 스레드에 "내가 지금 어떤 락을 기다리고 있는가" 정보가 필요해진다.

## 구현할 때 떠올려야 할 상태

스레드 쪽에 보통 필요해지는 것:
- 원래 우선순위 (`base priority`, 또는 `init_priority`)
- 현재 적용 우선순위 (`effective priority`, 혹은 기존 `priority`를 이 값으로 사용)
- 내가 기다리는 락 (`waiting_lock`)
- 나에게 우선순위를 기부한 donor 목록

락 쪽에서 봐야 할 것:
- `lock->holder`
- 이 락을 기다리는 스레드들

## 구현 포인트

### `lock_acquire()`에서 할 일

- 락을 잡으려는데 holder가 있고, 그 holder의 우선순위가 더 낮다면 donation을 시작한다.
- 현재 스레드의 높은 우선순위를 holder에 반영한다.
- nested 상황이면 `waiting_lock`을 따라 위로 전파한다.
- 그 다음 현재 스레드는 block 되어 락이 풀리길 기다린다.

### `lock_release()`에서 할 일

- 지금 반납하는 락 때문에 생겼던 donation을 정리한다.
- donor 목록에서 이 락과 관련된 donation들을 제거한다.
- 남아 있는 donor들과 base priority를 기준으로 effective priority를 다시 계산한다.
- 그 후 락 waiters 중 가장 높은 우선순위 스레드가 먼저 깨어나도록 해야 한다.

### `thread_set_priority()`에서 할 일

- donation이 없을 때는 base priority와 현재 priority를 함께 바꾸면 된다.
- donation이 있는 상태라면 "원래 우선순위만 변경"하고, 실제 실행 우선순위는 남아 있는 donation과 비교해서 다시 계산해야 한다.

## 테스트를 보는 순서

기본 우선순위 테스트:
- `priority-fifo`
- `priority-preempt`
- `priority-change`

그 다음 donation 테스트:
- `priority-donate-one`
- `priority-donate-multiple`
- `priority-donate-nest`

추천 흐름:
- 먼저 basic priority 테스트를 통과시킨다.
- 그 다음 `priority inversion` 개념과 `synch.c`의 lock 흐름을 다시 본다.
- 마지막으로 donation 관련 상태를 추가하고 `priority-donate-*` 테스트를 잡는다.

## 한 줄 요약

우선순위 기부는 "기부자가 점수를 잃는 시스템"이 아니라, 락을 쥔 스레드의 실행 우선순위를 일시적으로 끌어올려 priority inversion을 막는 시스템이다. 구현에서는 `base/effective priority` 분리, `waiting_lock`, donor 목록, 그리고 `lock_release()` 시 재계산이 핵심이다.
