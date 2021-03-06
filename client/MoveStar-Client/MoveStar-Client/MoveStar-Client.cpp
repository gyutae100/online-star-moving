// MoveStar-Client.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"

#pragma comment(lib, "ws2_32")

#include <WinSock2.h>
#include <Ws2tcpip.h>

#include <locale.h>    

#include <iostream>
using namespace std;

#define SLEEP_TIME 1000	//지연 시간

/**
brief 패킷 프로토콜.
*/
enum TYPE_PACKET {

	ID_ALLOC = 0,    ///<ID 할당
	CREATE_STAR = 1, ///<별 생성
	DELETE_STAR = 2, ///<별 삭제
	MOVE_STAR = 3, ///<별 이동
};


/**
brief 별의 이동 가능한 범위.
*/
enum MOVE_AVAILABLE {

	TOP = 0,		///<별 이동 범위 TOP
	BOTTOM = 23,	///<별 이동 범위 BOTTOM
	LEFT = 0,		///<별 이동 범위 LEFT
	RIGHT = 80		///<별 이동 범위 RIGHT
};

/**
brief 패킷 정의.
*/
struct PROTOCOL_PACKET {

	int _type;		///<패킷 프로토콜 타입
	int _id;		
	int _cur_x;
	int _cur_y;
};


/**
brief 별 구조체.
*/
struct STAR {

	BOOL _this_player;	///<해당 플레이어인 유무

	int _id;	///별의 ID

	int _cur_x;	///<현재 별의 위치 x
	int _cur_y;	///<현재 별의 위치 y
};


//게임 관련 변수.
int g_total_num_stars = 0;			///<별의 총 갯수.
STAR g_arr_stars[100] = { 0 , };	///<별 배열. 0으로 초기화.

int g_total_num_erase_stars = 0;		///<총 지울 별 갯수
STAR g_arr_erase_star[1000] = { 0 , };	//지울 별 배열. 

STAR *g_this_client_star=nullptr;		///<해당 클라이언트 별.
#define ID_THIS_CLIENT 0				///<해당 클라이언트 변수 인덱스.

static int g_last_total_num_star = 0;	///<마지막(최근) 존재하는 별의 갯수.


//콘솔 제어 변수.
HANDLE hConsole = NULL;

//콘솔 함수 목록.
void InitialColsole(void);
void MoveCursor(int iPosX, int iPosY);
void ClearScreen(void);

//핸들 함수 목록.
void HandleKeyInput();
void HandleNetwork(SOCKET *in_server_sock);
void HandleRendering();

//별 관리 함수 목록.
void AddStar(int id, int x, int y);
void MoveStar(int id, int x, int y);
void DeleteStar(int target_id);
void InitThisClientStar(int this_id);

//네트워크 관련 함수.
void DisconnectByServer(SOCKET server_sock);

//로직 관련 함수.
void DrawMapOutline(int start_x, int start_y);

//서버 네트워크 정보.
#define SERVER_PORT 3000
#define SERVER_IP L"127.0.0.1"

int main() {

	_wsetlocale(LC_ALL, L"korean");

	//윈속 초기화 한다.
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {

		return -1;
	}

	SOCKET server_sock= NULL;	///<서버 접속 소켓

	//콘솔 초기화한다.
	InitialColsole();

	//해당 클라이언트 별을 생성 및 초기화 한다.
	AddStar(-1, 40, 22);
	g_this_client_star = &g_arr_stars[ID_THIS_CLIENT];
	g_this_client_star->_this_player = TRUE;

	DrawMapOutline(-1, -1);

	//로직 루프.
	for (; ;) {
		 
		//키 입력을 처리한다.
		HandleKeyInput();

		//네트워크 처리한다.
		HandleNetwork(&server_sock);

		//랜더링 처리한다.
		HandleRendering();

		//시간 지연.
		Sleep(SLEEP_TIME);
	}

	return 0;
}



//키 입력을 처리한다.
void HandleKeyInput() {

	//지울 별 저장.
	g_arr_erase_star[g_total_num_erase_stars]._cur_x = g_this_client_star->_cur_x;
	g_arr_erase_star[g_total_num_erase_stars]._cur_y = g_this_client_star->_cur_y;
	g_total_num_erase_stars++;

	int inc_x=0;	///<x 이동량.
	int inc_y=0;	///<y 이동량.

	//현재 해당 클라이언트 위치 값.
	int cur_x = g_this_client_star->_cur_x;
	int cur_y = g_this_client_star->_cur_y;

	//------------------------------------------------------------------------------
	//	왼쪽 방향키 입력.
	//------------------------------------------------------------------------------
	if (GetAsyncKeyState(VK_LEFT) & 0x8001) {

		inc_x--;
	}

	//------------------------------------------------------------------------------
	//	오른쪽 방향키 입력.
	//------------------------------------------------------------------------------
	if (GetAsyncKeyState(VK_RIGHT) & 0x8001) {

		inc_x++;
	}

	//------------------------------------------------------------------------------
	//	아래 방향키 입력.
	//------------------------------------------------------------------------------
	if (GetAsyncKeyState(VK_DOWN) & 0x8001) {

		inc_y++;
	}

	//------------------------------------------------------------------------------
	//	위쪽 방향키 입력.
	//------------------------------------------------------------------------------
	if (GetAsyncKeyState(VK_UP) & 0x8001) {

		inc_y--;
	}

	//현 위치에서 증가량 합산.
	cur_x += inc_x;
	cur_y += inc_y;

	if (cur_x < MOVE_AVAILABLE::LEFT || cur_x > MOVE_AVAILABLE::RIGHT) {

		return;
	}

	if (cur_y < MOVE_AVAILABLE::TOP || cur_y > MOVE_AVAILABLE::BOTTOM) {

		return;
	}

	//이동한 최종 좌표.
	g_this_client_star->_cur_x = cur_x;
	g_this_client_star->_cur_y = cur_y;
}

//네트워크 처리한다.
void HandleNetwork(SOCKET *in_server_sock ) {

	int retval = 0;

	//데이터 통신에 사용할 변수.
	SOCKET server_sock = *in_server_sock;
	FD_SET rset, wset;
	SOCKET client_sock;
	SOCKADDR_IN server_addr;
	int addrlen, i;

	PROTOCOL_PACKET src_packet;


	if ( NULL == server_sock) {

		server_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (server_sock == INVALID_SOCKET) {

			_putws(L"socket 오류");
			return;
		}

		//넌블로킹 소켓으로 전환한다.
		u_long on = 1;
		retval = ioctlsocket(server_sock, FIONBIO, &on);
		if (retval == SOCKET_ERROR) {

			_putws(L"넌블로킹 오류");
			return;
		}


		*in_server_sock = server_sock;

		//소켓 설정한다.
		ZeroMemory(&server_addr, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		InetPton(AF_INET, SERVER_IP, &server_addr.sin_addr);
		server_addr.sin_port = htons(SERVER_PORT);

	
		//connect()
		//서버에 연결한다.
		retval = connect(server_sock, (SOCKADDR *)&server_addr, sizeof(server_addr));
		if (retval == SOCKET_ERROR) {

			//printf("%d ", GetLastError());
			//_putws(L"connect 에러");
			return ;
		}
	}

	//변경 좌표를 서버로 전송한다.
	src_packet._type = TYPE_PACKET::MOVE_STAR;
	src_packet._id = g_this_client_star->_id;
	src_packet._cur_x = g_this_client_star->_cur_x;
	src_packet._cur_y = g_this_client_star->_cur_y;

	int retval2 = -1;

	//------------------------------------------------------------------------------
	//	SEND MOVE My XY
	//
	//	:좌표 변경 안되도 보낸다.
	//------------------------------------------------------------------------------
	retval2 = send(server_sock, (char*)&src_packet, sizeof(src_packet), 0);

	//송신 버퍼에 문제가 발생할 일은 없지만... 
	//그래도 예외 처리!
	if (retval2 == SOCKET_ERROR) {
		
		DisconnectByServer(server_sock);
	}
	
	//송신 버퍼에 모든 데이터 옮긴 여부 체크.
	//송신 버퍼 깨짐 방지용.
	if (retval2 != sizeof(src_packet)) {
	
		DisconnectByServer(server_sock);
	}

	//셀렉트 모델 초기화 부분.
	FD_ZERO(&rset);
	FD_SET(server_sock, &rset);
	timeval select_time_val;
	select_time_val.tv_usec = 0;
	select_time_val.tv_sec = 0;

	//------------------------------------------------------------------------------
	//	SELECT PART!!!!!!!!!!!!!!!!!!!
	//------------------------------------------------------------------------------
	for ( ; ; ) {

		retval = select(server_sock, &rset, NULL, NULL, &select_time_val);
		
		if ( 0 == retval ) {
			
			return;
		}

		retval = FD_ISSET(server_sock, &rset);


		//recv가 존재하는 경우.
		if (retval > 0) {

			//------------------------------------------------------------------------------
			//	RECV FROM SERVER
			//------------------------------------------------------------------------------
			retval = recv(server_sock, (char*)&src_packet, sizeof(src_packet), 0);

			//핀 메시지가 오는 경우
			if (0 == retval) {

				DisconnectByServer(server_sock);
			}

			//수신 버퍼에 받은 데이터가 깨진 여부를 체크한다.
			if (retval != sizeof(src_packet)) {
				
				DisconnectByServer(server_sock);
			}

			//소켓 에러인 경우
			if (retval == SOCKET_ERROR) {

				DisconnectByServer(server_sock);
			}

	
			//----------------------------------
			//	패킷 처리 프로토콜 정의
			//	ID_ALLOC	: ID 할당	(수신)
			//	CREATE_STAR : 별 생성	(수신)
			//	DELETE_STAR : 별 삭제	(수신)
			//	MOVE_STAR	: 이동		(송신, 수신)
			//----------------------------------
			switch (src_packet._type) {


			//------------------------------------------------------------------------------
			//	ID 할당.
			//------------------------------------------------------------------------------
			case TYPE_PACKET::ID_ALLOC:

				//해당 클라이언트의 ID를 설정한다.
				InitThisClientStar(src_packet._id);
				MoveCursor(g_this_client_star->_cur_x, g_this_client_star->_cur_y);
				printf(" ");

				//정보 출력한다.
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM+1);
				printf("																  ");
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM +1 );
				printf("ALLOC %d %d %d", src_packet._id, src_packet._cur_x, src_packet._cur_y);
				break;

			//------------------------------------------------------------------------------
			//	별 생성.
			//------------------------------------------------------------------------------
			case TYPE_PACKET::CREATE_STAR:

				//이미 해당 클라이언트 별을 생성한 경우.
				if (g_this_client_star->_id == src_packet._id) {
				
					g_this_client_star->_cur_x = src_packet._cur_x;
					g_this_client_star->_cur_y = src_packet._cur_y;
				}
				//다른 클라이언트 별 설정.
				else {

					AddStar(src_packet._id, src_packet._cur_x, src_packet._cur_y);
				}

				//정보 출력한다.
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("																  ");
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("CREATE %d %d %d",  src_packet._id, src_packet._cur_x, src_packet._cur_y);
				break;

			//------------------------------------------------------------------------------
			//	별 삭제.
			//------------------------------------------------------------------------------
			case TYPE_PACKET::DELETE_STAR:

				DeleteStar(src_packet._id);

				//정보 출력한다.
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("																  ");
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("DELETE %d %d %d", src_packet._id, src_packet._cur_x, src_packet._cur_y);
				break;

			//------------------------------------------------------------------------------
			//	별 이동.
			//------------------------------------------------------------------------------
			case TYPE_PACKET::MOVE_STAR:

				if (g_this_client_star->_id == src_packet._id) {
				
					break;
				}

				MoveStar(src_packet._id, src_packet._cur_x, src_packet._cur_y);

				//정보 출력한다.
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("																  ");
				MoveCursor(0, MOVE_AVAILABLE::BOTTOM + 1);
				printf("MOVE %d %d %d", src_packet._id, src_packet._cur_x, src_packet._cur_y);
				break;
			}
		}
	}
}


//------------------------------------------------------------------------------
//	랜더링 처리한다.
//------------------------------------------------------------------------------
void HandleRendering() {

	int x;
	int y;

	//지울 별 삭제한다.
	for (int idx_cur_star = 0; idx_cur_star < g_total_num_erase_stars; idx_cur_star++) {

		x = g_arr_erase_star[idx_cur_star]._cur_x;
		y = g_arr_erase_star[idx_cur_star]._cur_y;
		MoveCursor(x, y);
		printf(" ");
															 
	}

	g_total_num_erase_stars = 0;


	//좌측 위 별 정보 목록 초기화한다.
	for (int i = 0; i < g_last_total_num_star+1; i++) {

		MoveCursor(20, MOVE_AVAILABLE::BOTTOM + 1 + i);
		printf("										 ");
	}


	//현재 좌표 별들을 다 그려준다.
	for (int idx_cur_star = 0; idx_cur_star < g_total_num_stars; idx_cur_star++) {


		x = g_arr_stars[idx_cur_star]._cur_x;
		y = g_arr_stars[idx_cur_star]._cur_y;
		MoveCursor(x, y);
		printf("*");

		//정보 출력한다.
		MoveCursor(20, MOVE_AVAILABLE::BOTTOM + 1 +idx_cur_star);
		printf("ID:[%3d] %3d.%3d ", g_arr_stars[idx_cur_star]._id , g_arr_stars[idx_cur_star]._cur_x, g_arr_stars[idx_cur_star]._cur_y);
	}

	g_last_total_num_star = g_total_num_stars;
}


//------------------------------------------------------------------------------
//	생성한 별을 별 배열에서 삽입해준다.
//------------------------------------------------------------------------------
void AddStar(int id , int x, int y) {

	g_arr_stars[g_total_num_stars]._id = id;	///<현재 별 id 설정
	g_arr_stars[g_total_num_stars]._cur_x = x;	///<현재 별의 위치 x
	g_arr_stars[g_total_num_stars]._cur_y = y;	///<현재 별의 위치 y

	g_total_num_stars++;
}

//별 좌표를 변경한다.
void MoveStar(int id, int x, int y) {

	for (int idx_cur_star = 0; idx_cur_star < g_total_num_stars; idx_cur_star++){	
	
		if (g_arr_stars[idx_cur_star]._id == id) {

			//지울 별 목록에 추가.
			g_arr_erase_star[g_total_num_erase_stars]._cur_x= g_arr_stars[idx_cur_star]._cur_x;
			g_arr_erase_star[g_total_num_erase_stars]._cur_y= g_arr_stars[idx_cur_star]._cur_y;
			g_total_num_erase_stars++;

			//변경된 좌표 저장.
			g_arr_stars[idx_cur_star]._cur_x = x;
			g_arr_stars[idx_cur_star]._cur_y = y;

			return;
		}
	}
}


//------------------------------------------------------------------------------
//	생성한 별을 별 배열에서 삭제해준다.
//------------------------------------------------------------------------------
void DeleteStar(int target_id) {

	int idx_delete = 0;

	for (int idx_cur_star = 0; idx_cur_star < g_total_num_stars; idx_cur_star++) {

		idx_delete = g_arr_stars[idx_cur_star]._id;

		if (idx_delete == target_id) {

			if (idx_cur_star != g_total_num_stars - 1) {

				//지울 별 저장.
				g_arr_erase_star[g_total_num_erase_stars]._cur_x = g_arr_stars[idx_cur_star]._cur_x;
				g_arr_erase_star[g_total_num_erase_stars]._cur_y = g_arr_stars[idx_cur_star]._cur_y;
				g_total_num_erase_stars++;

				g_arr_stars[idx_cur_star] = g_arr_stars[g_total_num_stars - 1];
				g_total_num_stars--;
			}
			else {

				//지울 별 저장.
				g_arr_erase_star[g_total_num_erase_stars]._cur_x = g_arr_stars[idx_cur_star]._cur_x;
				g_arr_erase_star[g_total_num_erase_stars]._cur_y = g_arr_stars[idx_cur_star]._cur_y;
				g_total_num_erase_stars++;

				g_total_num_stars--;
			}
		}	
	}
}


//------------------------------------------------------------------------------
//	해당 클라이언트의 ID를 설정한다.
//------------------------------------------------------------------------------
void InitThisClientStar(int this_id) {

	for (int idx_cur_star = 0; idx_cur_star < g_total_num_stars; idx_cur_star++) {

		if ( TRUE == g_arr_stars[idx_cur_star]._this_player) {
			
			g_arr_stars[idx_cur_star]._id = this_id;
			g_this_client_star = &g_arr_stars[idx_cur_star];
		}
	}
}



//------------------------------------------------------------------------------
//	서버에 의해 접속이 끊겨지는 경우.
//------------------------------------------------------------------------------
void DisconnectByServer(SOCKET server_sock) {

	closesocket(server_sock);
	WSACleanup();
	exit(1);
}

//------------------------------------------------------------------------------
//	맵 아웃 라인을 그려준다.
//------------------------------------------------------------------------------
void DrawMapOutline(int start_x, int start_y) {

}



//------------------------------------------------------------------------------
//	콘솔 제어 위한 준비 작업.
//------------------------------------------------------------------------------
void InitialColsole(void) {

	CONSOLE_CURSOR_INFO stConsoleCursor;

	//------------------------------------------------------------------------------
	//	화면 커서 안보이게 설정한다.
	//------------------------------------------------------------------------------
	stConsoleCursor.bVisible = FALSE;
	stConsoleCursor.dwSize = 1; ///<커서 크기 1

	//------------------------------------------------------------------------------
	//	콘솔화면(스텐다드 아웃풋) 핸들을 구한다.
	//------------------------------------------------------------------------------
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleCursorInfo(hConsole, &stConsoleCursor); //커서 세팅한다.
}

//------------------------------------------------------------------------------
//	콘솔 화면의 커서를 X, Y 좌표로 이동.
//------------------------------------------------------------------------------
void MoveCursor(int iPosX, int iPosY) {

	COORD stCoord;
	stCoord.X = iPosX;
	stCoord.Y = iPosY;

	SetConsoleCursorPosition(hConsole, stCoord);
}


//------------------------------------------------------------------------------
//	콘솔 화면을 클리어.
//------------------------------------------------------------------------------
void ClearScreen(void) {

	for (int y = MOVE_AVAILABLE::TOP; y < ::MOVE_AVAILABLE::BOTTOM; y++) {

		for (int x = MOVE_AVAILABLE::LEFT; x < MOVE_AVAILABLE::RIGHT; x++) {

			MoveCursor(x, y);
			printf(" ");
		}
	}
}
