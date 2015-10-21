너무 허접한 질문이였는지 조교님이 답변을 안주시네..
어제 삽질하면서 알아낸 테스트방법이다.

pintos/src/vm에서 make
cd build
pintos-mkdisk swap.dsk 4
pintos-mkdisk fs.dsk 2
pintos -f -q
pintos -p tests/userprog/wait-bad-pid -a wait-bad-pid -- -q
pintos run 'wait-bad-pid'

컴파일하면 pintos/src/vm/build/tests 안에
pintos/src/tests 폴더의 모든 테스트프로그램들의 '실행파일'이 들어가더라더고
우리는 그것들을 실행하면 돼..

so2010 코드들로 실행을 해보니 오류가 좀 있어서 수정했고..
음. 그렇게 실행한 결과들이 맞는 건지는 잘 모르겠어;;ㅠㅠ


---


참고로 make check를 해보려면 몇가지 변경해야할게 있는데

우선 boch폴더로 가

cd ~/boch-xx-xx (버전 기억 안남..)
./configure --with-nogui
> (예전에 boch 설치할땐 --enable-gdb-stub으로 했었는데 그건 디버깅이 가능하도록 하는 거고, 저건 make check가 가능하게 하는거라 생각하면 돼)
make
sudo make install

그렇게 boch를 재설치하고

pintos/src/userprog/Make.vars
pintos/src/vm/Make.vars
pintos/src/filesys/Make.vars
파일의 SIMULATOR = --qemu 부분을 SIMULATOR = --bochs로 바꿔야해.
그건 디버깅을 하건 make check를 하건 계속 바꿔놓아도 됌.

그렇게 한 후 프로젝트2(Threads)에 대한걸 make check 해보면 몇가지 pass가 뜨는걸 확인할 수 있을거야

cd pintos/src/threads
make
cd build
make check

프로젝트3,4는 make후 build 폴더 들어가서 disk 만들거나 하지 말고 바로 make check 누르면 돼

그런데 이상하게 다 fail 뜨네..

결과를 보니 결과값은 맞게 나오는것 같은데 '출력양식'이 좀 달라서그런것 같아.
왜 출력양식이 다르게 나오는지는 원인을 모르겠음.;'

여튼 make check는 안해본다고 하셨으니 크게 신경안써도 될듯