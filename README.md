## RingBuffer
RingBuffer Code for Game Server Programming

x86, MSVC 환경 (TSO Memory Order)에서  
Single Producer - Single Consumer로 사용할 시에  
Lock Free로 사용 가능한 RingBuffer Queue입니다.

## Logger
Logger for game server

위의 RingBuffer를 활용하여 작성한 Logger입니다.  
현재 Thread-safe 부분은 지원하지 않습니다.  
추후에 Thread-safe 관련 기능을 추가할 예정입니다
