#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

const int PORT=45000, MAXC=20, MAXR=10;
mutex gameMtx, coutMtx;

struct Client{ int fd=-1,room=-1; string nick; char role='-',symbol='-'; } c[MAXC];
struct Room{ bool used=false,over=false; int x=-1,o=-1; char b[9]={'-','-','-','-','-','-','-','-','-'},turn='X'; } r[MAXR];

string pad(int n,int k){ string s=to_string(n); return string(k-s.size(),'0')+s; }
void protocol(string t,string d,string msg){ lock_guard<mutex> l(coutMtx); cout<<"\n["<<t<<" PROTOCOL] "<<d<<"\n-> "<<msg<<"\n"; }
bool readN(int s,char *b,int n){ int x,t=0; while(t<n&&(x=recv(s,b+t,n-t,0))>0)t+=x; return t==n; }
void sendP(int s,string m,string msg){ send(s,m.data(),m.size(),0); protocol("TX",m,msg); }

bool win(Room &g){ int w[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}}; for(auto &x:w) if(g.b[x[0]]==g.turn&&g.b[x[1]]==g.turn&&g.b[x[2]]==g.turn)return true; return false; }
bool draw(Room &g){ for(char x:g.b) if(x=='-') return false; return true; }

void roomSend(int id,string m,string msg){
    for(int i=0;i<MAXC;i++) if(c[i].fd!=-1&&c[i].room==id) send(c[i].fd,m.data(),m.size(),0);
    protocol("TX",m,msg);
}
void board(int id){ string m="TT"; m.append(r[id].b,9); roomSend(id,m,"Tablero actualizado de la sala "+to_string(id+1)+"."); }
void turn(int id){ string m="Tt"; m+=r[id].turn; roomSend(id,m,"Turno del jugador "+string(1,r[id].turn)+"."); }

string roomsText(){
    string s;
    for(int i=0;i<MAXR;i++) if(r[i].used){
        if(!s.empty()) s+=" | ";
        s+=pad(i+1,2)+" ["+(r[i].x!=-1?c[r[i].x].nick:"-")+" vs "+(r[i].o!=-1?c[r[i].o].nick:"esperando")+"]";
    }
    return s.empty()?"sin salas":s;
}
void sendRooms(int id){ string x=roomsText(),m="Tr"+pad(x.size(),3)+x; sendP(c[id].fd,m,"Lista de salas enviada."); }
string usersText(){ string s; for(int i=0;i<MAXC;i++) if(c[i].fd!=-1&&!c[i].nick.empty()){ if(!s.empty())s+=","; s+=c[i].nick; } return s; }
void sendUsers(int id){ string x=usersText(),m="l"+pad(x.size(),17)+x; sendP(c[id].fd,m,"Lista de usuarios enviada."); }

int getRoom(){
    for(int i=0;i<MAXR;i++) if(r[i].used&&!r[i].over&&r[i].x!=-1&&r[i].o==-1) return i;
    for(int i=0;i<MAXR;i++) if(!r[i].used){ r[i]=Room(); r[i].used=true; return i; }
    return -1;
}

void clientTask(int id){
    int s=c[id].fd; char a,len[7];
    if(!readN(s,&a,1)||a!='N'||!readN(s,len,7)) return;
    int n=stoi(string(len,7)); string nick(n,' '); readN(s,&nick[0],n); c[id].nick=nick;
    protocol("RX","N"+string(len,7)+nick,"Nickname recibido: "+nick+".");

    char reg[2]; if(!readN(s,reg,2)) return; c[id].role=reg[1];
    protocol("RX",string(reg,2),c[id].role=='P'?"El usuario quiere jugar.":"El usuario entra como espectador.");

    if(c[id].role=='P'){
        lock_guard<mutex> l(gameMtx);
        int k=getRoom(); c[id].room=k;
        if(r[k].x==-1){ r[k].x=id; c[id].symbol='X'; }
        else{ r[k].o=id; c[id].symbol='O'; }
        sendP(s,"TS"+pad(k+1,2)+c[id].symbol,"Asignado a la sala "+to_string(k+1)+" como "+c[id].symbol+".");
        if(r[k].o==-1) sendP(s,"TQ"+pad(k+1,2),"Esperando al segundo jugador en la sala "+to_string(k+1)+".");
        else{
            r[k].turn=(rand()%2?'X':'O');
            roomSend(k,"TG"+pad(k+1,2),"La partida de la sala "+to_string(k+1)+" comenzo.");
            board(k); turn(k);
        }
    }else sendRooms(id);

    while(1){
        char h;
        if(!readN(s,&h,1)) break;

        if(h=='Q'){ protocol("RX","Q",c[id].nick+" salio del servidor."); break; }
        if(h=='L'){ protocol("RX","L",c[id].nick+" solicita usuarios."); lock_guard<mutex> l(gameMtx); sendUsers(id); continue; }

        if(h!='T') continue;
        char op; if(!readN(s,&op,1)) break;

        if(op=='R'){
            protocol("RX","TR",c[id].nick+" solicita las salas.");
            lock_guard<mutex> l(gameMtx); sendRooms(id);
        }
        else if(op=='J'){
            char rr[2]; readN(s,rr,2); string m="TJ"+string(rr,2); protocol("RX",m,c[id].nick+" quiere entrar a una sala.");
            int k=stoi(string(rr,2))-1; lock_guard<mutex> l(gameMtx);
            if(k>=0&&k<MAXR&&r[k].used){
                c[id].room=k; sendP(s,"Tj"+pad(k+1,2),"Entraste a la sala "+to_string(k+1)+" como espectador.");
                string b="TT"; b.append(r[k].b,9); sendP(s,b,"Tablero actual de la sala.");
                if(r[k].x!=-1&&r[k].o!=-1){ string t="Tt"; t+=r[k].turn; sendP(s,t,"Turno actual: "+string(1,r[k].turn)+"."); }
            }
        }
        else if(op=='M'){
            char p; readN(s,&p,1); string m="TM"+string(1,p); protocol("RX",m,c[id].nick+" solicita mover a la posicion "+string(1,p)+".");
            lock_guard<mutex> l(gameMtx); int k=c[id].room;
            if(k<0||c[id].role!='P'||c[id].symbol!=r[k].turn) continue;
            int pos=p-'1';
            if(pos<0||pos>8||r[k].b[pos]!='-'){ sendP(s,"TEPT","Esa posicion ya esta ocupada."); continue; }
            r[k].b[pos]=r[k].turn; board(k);
            if(win(r[k])){
                int w=(r[k].turn=='X'?r[k].x:r[k].o), lo=(r[k].turn=='X'?r[k].o:r[k].x);
                sendP(c[w].fd,"TW","Ganaste la partida."); sendP(c[lo].fd,"TO","Perdiste la partida."); r[k].over=true;
            }else if(draw(r[k])){ roomSend(k,"TD","La partida termino en empate."); r[k].over=true; }
            else{ r[k].turn=(r[k].turn=='X'?'O':'X'); turn(k); }
        }
    }
    close(s); lock_guard<mutex> l(gameMtx); c[id].fd=-1;
}

void showIP(){
    int s=socket(AF_INET,SOCK_DGRAM,0); sockaddr_in x{},me{}; socklen_t n=sizeof(me);
    x.sin_family=AF_INET; x.sin_port=htons(53); inet_pton(AF_INET,"8.8.8.8",&x.sin_addr);
    connect(s,(sockaddr*)&x,sizeof(x)); getsockname(s,(sockaddr*)&me,&n);
    cout<<"SERVER RUNNING\nLocal: ./client\nNetwork: ./client "<<inet_ntoa(me.sin_addr)<<"\nPort: "<<PORT<<"\n"; close(s);
}

int main(){
    srand(time(0)); int server=socket(AF_INET,SOCK_STREAM,0); sockaddr_in a{}; socklen_t n=sizeof(a);
    a.sin_family=AF_INET; a.sin_port=htons(PORT); a.sin_addr.s_addr=INADDR_ANY;
    bind(server,(sockaddr*)&a,sizeof(a)); listen(server,10); showIP(); cout<<"Esperando conexiones...\n";
    while(1){
        int s=accept(server,(sockaddr*)&a,&n),id=0; while(id<MAXC&&c[id].fd!=-1)id++;
        if(id==MAXC){ close(s); continue; } c[id].fd=s; thread(clientTask,id).detach();
    }
}
