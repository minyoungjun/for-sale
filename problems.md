현재 load()에서 validate\_segment()를 호출 하는데 그 리턴값이 false이 나온다.
(어떤 프로그램을 실행하던지..)

리턴값이 true가 나와 load\_segment()가 호출되어야 하는데

무조건 false가 나와 프로그램이 실행조차 되지 않는다. (phdr->p\_vaddr < PGSIZE 때문)

즉, 지금까지 만든 코드를 하나도 테스트 해 볼 수가 없다.