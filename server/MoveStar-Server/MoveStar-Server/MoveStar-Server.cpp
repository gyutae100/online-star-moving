// MoveStar-Server.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"

#include "stdafx.h"

#pragma comment(lib, "ws2_32")

#include <WinSock2.h>
#include <Ws2tcpip.h>

#include <locale.h>    

#include <iostream>
using namespace std;

#define SLEEP_TIME 0


//서버 네트워크 주소 설정.
#define SERVER_PORT 3000
#define SERVER_IP L"0.0.0.0"

/**
brief 패킷 프로토콜 종류.
*/
enum TYPE_PACKET {

	ID_ALLOC = 0,    ///<ID 할당
	CREATE_STAR = 1, ///<별 생성
	DELETE_STAR = 2, ///<별 삭제
	MOVE_STAR = 3,	 ///<별 이동
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
brief 별의 이동 가능한 범위.
*/
enum MOVE_AVAILABLE {

	TOP = 0,		///<별 이동 범위 TOP
	BOTTOM = 23,	///<별 이동 범위 BOTTOM
	LEFT = 0,		///<별 이동 범위 LEFT
	RIGHT = 80		///<별 이동 범위 RIGHT
};

/**
brief 플레이어 구조체.
*/
struct Player {

	SOCKET _socket;				//소켓. 
		
	SOCKADDR_IN _sock_addr;		//ip, port. 해당 코드에서는 안 쓰는 중.
	
	int _id;	///별의 ID

	int _cur_x;	///<현재 별의 위치 x
	int _cur_y;	///<현재 별의 위치 y
};


//루프 관련 변수.
int g_now_idx_player = -1;						///<별의 총 갯수.
const int g_max_num_player = 100;				///<최대 플레이어 갯수
Player g_arr_player[g_max_num_player] = { 0 , };///<플레이어 배열

int g_total_num_erase_stars = 0;						///<총 지울 별 갯수
Player g_arr_erase_star[g_max_num_player] = { 0 , };	//지울 별 배열. 

//서버 실행 함수.
BOOL OnServer(SOCKET *in_out_listen_sock);

//핸들 함수 목록.
void HandleNetwork(SOCKET *in_server_sock);
void HandleRendering();

//Proc 함수 목록.
void ProcAccept(SOCKET in_listen_sock);
void ProcRecv(FD_SET in_rest);

//클라이언트 접속 해제 함수 목록.
void DisconnectByClient(SOCKET client_sock);
void ProcDisconnectClient(Player *target_player);

//Packet 함수 목록.
BOOL SendUnicastPacket(Player *terget_player, PROTOCOL_PACKET *src_packet, int len_byte_packet);
BOOL SendBroadCastPacketWithOut(Player *except_player, PROTOCOL_PACKET *src_packet, int len_byte_packet);

//클라이언트 관리 함수 목록.
void AddClient(int id, int x, int y);
void DeleteClient(int target_id);

//콘솔 제어 변수.
HANDLE g_handle_console = NULL;

//콘솔 함수 목록.
void InitialColsole(void);
void MoveCursor(int iPosX, int iPosY);
void ClearScreen(void);


int main(){

	//문자 한국어 설정.
	_wsetlocale(LC_ALL, L"korean");

	//콘솔 초기화한다.
	InitialColsole();

	//리슨 소켓.
	SOCKET listen_sock= NULL;

	//------------------------------------------------------------------
	//	Server On
	//------------------------------------------------------------------
	if (FALSE == OnServer(&listen_sock))
	{
		//예외처리
		_putws(L" 서버 실행 오류");
		return FALSE;
	}

	//------------------------------------------------------------------
	//	로직 루프
	//------------------------------------------------------------------
	for (; ;) {

		//------------------------------------------------------------------
		//	네트워크 처리한다.
		//------------------------------------------------------------------
		HandleNetwork(&listen_sock);

		//------------------------------------------------------------------
		//	랜더링 처리한다.
		//------------------------------------------------------------------
		HandleRendering();

		//------------------------------------------------------------------
		//	지연 설정. 
		//	:서버에서 사실상 필요x
		//------------------------------------------------------------------
		//Sleep(SLEEP_TIME);
	}

    return 0;
}


//------------------------------------------------------------------
//	네트워크 처리한다.
//------------------------------------------------------------------
void HandleNetwork(SOCKET *in_out_listen_sock) {

	int retval = 0;

	SOCKET listen_sock=NULL;
	listen_sock = *in_out_listen_sock;

	//리슨 소켓 체크.
	if (NULL == listen_sock) {

		_putws(L"리슨 소켓 널 값 오류");
		return;
	}

	//셀렉트 데이터 통신에 사용할 변수 목록.
	FD_SET rset;
	FD_SET expception_set;
	SOCKET client_sock;

	//리슨 소켓 FD_SET 설정.
	FD_ZERO(&rset);
	FD_SET(listen_sock, &rset);

	//모든 접속한 클라이언트 FD_SET 설정.
	for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

		FD_SET(g_arr_player[idx_player]._socket, &rset);
	}

	//------------------------------------------------------------------
	//	select@_@ 고고고.
	//
	//	:추후 FIN 메시지는 except로 받아보자~~
	//------------------------------------------------------------------
	TIMEVAL time_val;		
	time_val.tv_sec = 0;
	time_val.tv_usec = 0;
	for (; ; ) {

		retval = select(0, &rset, NULL, NULL, &time_val); 
		if (retval == SOCKET_ERROR) {

			return;
		}
		if (0 >= retval) {

			//_putws(L"셀렉트 오류");
			return;
		}

		//------------------------------------------------------------------
		//	accep 처리.
		//------------------------------------------------------------------
		if (FD_ISSET(listen_sock, &rset)) {

			ProcAccept(listen_sock);
			continue;
		}
	
		//------------------------------------------------------------------
		//	모든 클라이언트 리시브 처리.
		//------------------------------------------------------------------
		ProcRecv(rset);
	}
}

//------------------------------------------------------------------
//	랜더링 처리한다.
//------------------------------------------------------------------
void HandleRendering() {

	int x;
	int y;

	//좌측 위 별 정보 목록 초기화한다.
	//리소스 많이 잡아 먹어서 꺼뒀다ㅠㅠ
	//for (int i = 1; i <= g_now_idx_player+1; i++) {

	//	MoveCursor(0, i);
	//	puts("													");
	//}

	MoveCursor(0, 0);
	printf("CONNECTION : %3d", g_now_idx_player+1);

	//지울 별 삭제한다.
	for (int idx_player = 0; idx_player < g_total_num_erase_stars; idx_player++) {

		x = g_arr_erase_star[idx_player]._cur_x;
		y = g_arr_erase_star[idx_player]._cur_y;
		MoveCursor(x, y);
		printf(" ");
	}

	g_total_num_erase_stars = 0;


	//------------------------------------------------------------------
	//	현재 좌표 별들을 다 그려준다.
	//------------------------------------------------------------------
	for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

		x = g_arr_player[idx_player]._cur_x;
		y = g_arr_player[idx_player]._cur_y;
		MoveCursor(x, y);
		printf("*");

		//정보 출력한다. 리소스를 너무 많이 잡아먹는다 ㅠㅠ 그래서 주석 처리 해둠...
		//MoveCursor(0, idx_player+1);
		//printf("[%d] [%d] %d %d ", idx_player, g_arr_player[idx_player]._id, g_arr_player[idx_player]._cur_x, g_arr_player[idx_player]._cur_y);
	}

	return;

}

//------------------------------------------------------------------
//	Accept 처리한다.
//------------------------------------------------------------------
void ProcAccept(SOCKET in_listen_sock) {

	//데이터 통신에 사용할 변수
	FD_SET rset;
	FD_SET expception_set;
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen, i;

	addrlen = sizeof(clientaddr);
	client_sock = accept(in_listen_sock, (SOCKADDR *)&clientaddr, &addrlen);
	if (client_sock == INVALID_SOCKET) {

		_putws(L"accept() 오류");
		return;
	}
	else {

		g_now_idx_player++;
		int cur_idx = g_now_idx_player;

		//ID 할당 패킷 설정.
		g_arr_player[g_now_idx_player]._socket = client_sock;
		g_arr_player[g_now_idx_player]._id = g_now_idx_player;
		g_arr_player[g_now_idx_player]._sock_addr = clientaddr;
		g_arr_player[g_now_idx_player]._cur_x = 5;
		g_arr_player[g_now_idx_player]._cur_y = 5;

		PROTOCOL_PACKET src_packet;

		//------------------------------------------------------------------
		//	accep한 클라이언트에게 ID할당 패킷 송신한다.
		//------------------------------------------------------------------
		src_packet._type = TYPE_PACKET::ID_ALLOC;
		src_packet._id = g_now_idx_player;

		SendUnicastPacket(&g_arr_player[g_now_idx_player], &src_packet, sizeof(src_packet));

		//------------------------------------------------------------------
		//	aceept한 클라이언트에게 기존 클라이언트 정보를 송신한다.
		//------------------------------------------------------------------
		src_packet._type = TYPE_PACKET::CREATE_STAR;
		for (int idx_player = 0; idx_player < g_now_idx_player; idx_player++) {
		
			src_packet._id = g_arr_player[idx_player]._id;
			src_packet._cur_x = g_arr_player[idx_player]._cur_x;
			src_packet._cur_y = g_arr_player[idx_player]._cur_y;

			SendUnicastPacket(&g_arr_player[g_now_idx_player], &src_packet, sizeof(src_packet));
		}

		//------------------------------------------------------------------
		//	모든 클라이언트에게 새로운 클라이언트 생성 패킷을 송신한다.
		//------------------------------------------------------------------
		src_packet._type = TYPE_PACKET::CREATE_STAR;
		src_packet._id = g_arr_player[g_now_idx_player]._id;
		src_packet._cur_x = g_arr_player[g_now_idx_player]._cur_x;
		src_packet._cur_y = g_arr_player[g_now_idx_player]._cur_y;

		SendBroadCastPacketWithOut(NULL, &src_packet, sizeof(src_packet));
	}
}


//------------------------------------------------------------------
//	모든 클라이언트 Recv 처리한다.
//------------------------------------------------------------------
void ProcRecv(FD_SET in_rest) {


	FD_SET rset = in_rest;
	
	int retval = -1;

	BOOL check_fin = false;

	//별 이동 패킷을 수신한 경우를 체크한다.
	//수신 한 경우 해당 클라이언트를 제외하고 송신한다.
	for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

		check_fin = false;

		SOCKET recv_socket = g_arr_player[idx_player]._socket;

		PROTOCOL_PACKET tmp_packet;

		int recv_id;
		int delete_id;

		//recv 여부 체크한다.
		if (FD_ISSET(recv_socket, &rset)) {

			retval =recv(recv_socket, (char *)&tmp_packet, sizeof(tmp_packet), 0);
			recv_id = tmp_packet._id;

			//------------------------------------------------------------------
			//	특정 클라이언트가 FIN메시지를 보낸 경우 체크.
			//------------------------------------------------------------------
			if (0 == retval || SOCKET_ERROR ==retval) {

				retval = GetLastError();

				int idx_player = 0;

				for (idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {
					
					//해당 클라이언트 삭제 패킷을 다른 클라이언트에게 전송한다.
					if (recv_socket == g_arr_player[idx_player]._socket){

						ProcDisconnectClient(&g_arr_player[idx_player]);
						delete_id = idx_player;

						check_fin = true; //or continue;

						break;
					}
				}
			}

			//------------------------------------------------------------------------
			//	이동 패킷을 수신한 경우 다른 클라이언트에게 이동 패킷을 전송해야 한다.
			//------------------------------------------------------------------------
			if (check_fin == false) {

				//지울 별 저장.
				g_arr_erase_star[g_total_num_erase_stars]._cur_x = g_arr_player[recv_id]._cur_x;
				g_arr_erase_star[g_total_num_erase_stars]._cur_y = g_arr_player[recv_id]._cur_y;
				g_total_num_erase_stars++;


				g_arr_player[recv_id]._cur_x = tmp_packet._cur_x;
				g_arr_player[recv_id]._cur_y = tmp_packet._cur_y;
	
				SendBroadCastPacketWithOut(&g_arr_player[recv_id], &tmp_packet, sizeof(tmp_packet));
				}
		}
	}
}


//------------------------------------------------------------------------
//	Send Unicast
//------------------------------------------------------------------------
BOOL SendUnicastPacket(Player *terget_player, PROTOCOL_PACKET* src_packet, int len_byte_packet) {

	SOCKET tmp_socket = terget_player->_socket;
	int retval = -1;

	retval = send(tmp_socket, (char*)src_packet, sizeof(*src_packet), 0);

	if (retval == len_byte_packet){
		
		return true;
	}
	else {

		return false;
	}
}

//------------------------------------------------------------------------
//	Send BroadCast WithOut Something or ALL
//------------------------------------------------------------------------
BOOL SendBroadCastPacketWithOut(Player *except_player, PROTOCOL_PACKET *src_packet, int len_byte_packet) {
	
	//예외 ID.
	int id_except = -1;

	if (except_player != NULL)
	{
		id_except = except_player->_id;
	}

	int retval=-1;

	SOCKET tmp_socket=NULL;

	//------------------------------------------------------------------------
	//	Send BroadCast ALL
	//------------------------------------------------------------------------
	if (NULL == except_player) {

		for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

			SendUnicastPacket(&g_arr_player[idx_player], src_packet, sizeof(*src_packet));
			
		}

	}
	//------------------------------------------------------------------------
	//	Send BroadCast WithOut Something
	//------------------------------------------------------------------------
	else {
	
		PROTOCOL_PACKET tmp_paket = *src_packet;
		for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

			tmp_paket._id = g_arr_player[idx_player]._id;

			if (tmp_paket._id != id_except)
			{
				SendUnicastPacket(&g_arr_player[idx_player], src_packet, sizeof(*src_packet));
			}
		}

	}

	if (retval == len_byte_packet) {

		return true;
	}
	else {

		return false;
	}


}

//------------------------------------------------------------------------
//	생성한 별을 별 배열에서 삽입해준다.
//------------------------------------------------------------------------
void AddStar(int id, int x, int y) {


}

//------------------------------------------------------------------------
//	생성한 별을 별 배열에서 삭제해준다.
//------------------------------------------------------------------------
void DeleteClient(int target_id) {

	int idx_delete = 0;

	for (int idx_player = 0; idx_player <= g_now_idx_player; idx_player++) {

		idx_delete = g_arr_player[idx_player]._id;

		if (idx_delete == target_id) {

			if (idx_player != g_now_idx_player ) {

				g_arr_player[idx_player]._socket = INVALID_SOCKET;
				g_arr_player[idx_player] = g_arr_player[g_now_idx_player];
				g_now_idx_player--;
			}
			else {

				g_now_idx_player--;
			}
		}
	}
}

//------------------------------------------------------------------------
//	서버 작동.
//------------------------------------------------------------------------
BOOL OnServer(SOCKET *in_out_listen_sock) {

	int retval = -1;

	//윈속 초기화 한다.
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {

		return FALSE;
	}

	//리슨 용도 소켓 생성한다.
	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) {

		_putws(L"socket 오류");
		return FALSE;
	}

	*in_out_listen_sock = listen_sock;

	//리슨 소켓 바인드한다.
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	InetPton(AF_INET, SERVER_IP, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(SERVER_PORT);
	retval = bind(listen_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) {

		_putws(L"bind 오류");
		return FALSE;
	}

	//listen()
	//리슨 소켓 활성화한다.
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) {

		_putws(L"리슨 소켓 오류");
		return FALSE;
	}

	u_long on;
	//리슨 소켓을 넌블로킹 소켓으로 전환.
	retval = ioctlsocket(listen_sock, FIONBIO, &on);
	if (retval == SOCKET_ERROR) {

		_putws(L"리슨 넌블로킹 전환 오류");
		return FALSE;
	}

	//플레이어 리스트 초기화.
	for (int idx_player = 0; idx_player <= g_max_num_player; idx_player++) {

		g_arr_player[idx_player]._socket = INVALID_SOCKET;
	}

	return TRUE;
}



//------------------------------------------------------------------------
//	클라이언트 FIN에 의해 연결 끊기는 경우.
//------------------------------------------------------------------------
void DisconnectByClient(SOCKET client_sock) {

	closesocket(client_sock);
}


//------------------------------------------------------------------------
//	클라이언트 FIN에 의해 연결 끊기는 경우.
//------------------------------------------------------------------------
void ProcDisconnectClient(Player *target_player) {

	//다른 클라이언트에게 특정 클라이언트가 종료되었음을 알린다.
	PROTOCOL_PACKET fin_packet;
	fin_packet._type = TYPE_PACKET::DELETE_STAR;
	fin_packet._id = target_player->_id;
	SendBroadCastPacketWithOut(target_player, &fin_packet, sizeof(fin_packet));

	//지울 별 저장.
	g_arr_erase_star[g_total_num_erase_stars]._cur_x = target_player->_cur_x;
	g_arr_erase_star[g_total_num_erase_stars]._cur_y = target_player->_cur_y;
	g_total_num_erase_stars++;

	//해당 유저 삭제.
	DeleteClient(target_player->_id);

	//해당 유저 연결 해제.
	DisconnectByClient(target_player->_socket);

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
	g_handle_console = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleCursorInfo(g_handle_console, &stConsoleCursor); //커서 세팅한다.
}

//------------------------------------------------------------------------------
//	콘솔 화면의 커서를 X, Y 좌표로 이동.
//------------------------------------------------------------------------------
void MoveCursor(int iPosX, int iPosY) {

	COORD stCoord;
	stCoord.X = iPosX;
	stCoord.Y = iPosY;

	SetConsoleCursorPosition(g_handle_console, stCoord);
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
