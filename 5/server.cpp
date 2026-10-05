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
struct Room{ bool used=false,over=false; string name; int x=-1,o=-1; char b[9]={'-','-','-','-','-','-','-','-','-'},turn='X'; } r[MAXR];

string pad(int n,int k){ string s=to_string(n); return string(k-s.size(),'0')+s; }
void protocol(string t,string d,string msg){ lock_guard<mutex> l(coutMtx); cout<<"\n["<<t<<" PROTOCOL] "<<d<<"\n-> "<<msg<<"\n"; }
bool readN(int s,char *b,int n){ int x,t=0; while(t<n&&(x=recv(s,b+t,n-t,0))>0)t+=x; return t==n; }
string readText(int s,int k){ string x(k,' '); if(k) readN(s,&x[0],k); return x; }
void sendP(int s,string m,string msg){ send(s,m.data(),m.size(),0); protocol("TX",m,msg); }

bool win(Room &g){ int w[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}}; for(auto &x:w) if(g.b[x[0]]==g.turn&&g.b[x[1]]==g.turn&&g.b[x[2]]==g.turn)return true; return false; }
bool draw(Room &g){ for(char x:g.b) if(x=='-') return false; return true; }

void roomSend(int k,string m,string msg){
    for(int i=0;i<MAXC;i++) if(c[i].fd!=-1&&c[i].room==k) send(c[i].fd,m.data(),m.size(),0);
    protocol("TX",m,msg);
}
void board(int k){ string m="TT"; m.append(r[k].b,9); roomSend(k,m,"Tablero actualizado de la sala "+r[k].name+"."); }
void turn(int k){ string m="Tt"; m+=r[k].turn; roomSend(k,m,"Turno del jugador "+string(1,r[k].turn)+" en la sala "+r[k].name+"."); }

string usersText(){ string s; for(int i=0;i<MAXC;i++) if(c[i].fd!=-1&&!c[i].nick.empty()){ if(!s.empty())s+=" | "; s+=c[i].nick; if(c[i].role=='P')s+="(Jugador)"; else if(c[i].role=='V')s+="(Viewer)"; } return s.empty()?"sin usuarios":s; }
void sendUsers(int id){ string x=usersText(); sendP(c[id].fd,"l"+pad(x.size(),17)+x,"Lista de jugadores/usuarios enviada a "+c[id].nick+"."); }

string roomsText(){
    string s;
    for(int i=0;i<MAXR;i++) if(r[i].used&&!r[i].over){
        if(!s.empty()) s+=" | ";
        s+=r[i].name+" ["+(r[i].o==-1?string("esperando jugador"):string("en juego"))+"]";
    }
    return s.empty()?"sin salas":s;
}
void sendRooms(int id){ string x=roomsText(); sendP(c[id].fd,"Tr"+pad(x.size(),3)+x,"Lista de salas enviada a "+c[id].nick+"."); }
int findRoom(string name){ for(int i=0;i<MAXR;i++) if(r[i].used&&!r[i].over&&r[i].name==name) return i; return -1; }

void createRoom(int id){
    int k=0; while(k<MAXR&&r[k].used)k++; if(k==MAXR)return;
    r[k]=Room(); r[k].used=true; r[k].name=c[id].nick; r[k].x=id;
    c[id].room=k; c[id].symbol='X';
    string n=r[k].name;
    sendP(c[id].fd,"TS"+pad(n.size(),7)+n+"X","Sala '"+n+"' creada. Juegas con X.");
    sendP(c[id].fd,"TQ"+pad(n.size(),7)+n,"Esperando otro jugador en la sala '"+n+"'.");
}

void joinRoom(int id,string name){
    int k=findRoom(name); if(k<0)return;
    c[id].room=k;
    if(c[id].role=='V'){
        sendP(c[id].fd,"Tj"+pad(name.size(),7)+name,"Entraste a la sala '"+name+"' como espectador.");
        string b="TT"; b.append(r[k].b,9); sendP(c[id].fd,b,"Tablero actual de la sala '"+name+"'.");
        if(r[k].o!=-1){ string t="Tt"; t+=r[k].turn; sendP(c[id].fd,t,"Turno actual: "+string(1,r[k].turn)+"."); }
        return;
    }
    if(r[k].o!=-1)return;
    r[k].o=id; c[id].symbol='O';
    sendP(c[id].fd,"TS"+pad(name.size(),7)+name+"O","Te uniste a la sala '"+name+"'. Juegas con O.");
    r[k].turn=(rand()%2?'X':'O');
    roomSend(k,"TG"+pad(name.size(),7)+name,"La partida de la sala '"+name+"' comenzo.");
    board(k); turn(k);
}

void clientTask(int id){
    int s=c[id].fd; char a,len[7];
    if(!readN(s,&a,1)||a!='N'||!readN(s,len,7)) return;
    int n=stoi(string(len,7)); c[id].nick=readText(s,n);
    protocol("RX","N"+string(len,7)+c[id].nick,"Nickname recibido: "+c[id].nick+".");

    while(1){
        char h; if(!readN(s,&h,1))break;
        if(h=='Q'){ protocol("RX","Q",c[id].nick+" salio del servidor."); break; }
        if(h=='L'){ protocol("RX","L",c[id].nick+" solicita la lista de jugadores."); lock_guard<mutex> l(gameMtx); sendUsers(id); continue; }
        if(h!='T')continue;

        char op; if(!readN(s,&op,1))break; string base="T"+string(1,op);
        if(op=='P'||op=='V'){
            lock_guard<mutex> l(gameMtx); c[id].role=op;
            protocol("RX",base,c[id].nick+(op=='P'?" se registro como JUGADOR.":" se registro como VIEWER."));
        }
        else if(op=='C'){
            protocol("RX",base,c[id].nick+" solicita crear una sala."); lock_guard<mutex> l(gameMtx); createRoom(id);
        }
        else if(op=='R'){
            protocol("RX",base,c[id].nick+" solicita ver las salas."); lock_guard<mutex> l(gameMtx); sendRooms(id);
        }
        else if(op=='J'){
            char z[7]; readN(s,z,7); int k=stoi(string(z,7)); string name=readText(s,k);
            protocol("RX",base+string(z,7)+name,c[id].nick+" solicita entrar a la sala '"+name+"'.");
            lock_guard<mutex> l(gameMtx); joinRoom(id,name);
        }
        else if(op=='M'){
            char p; readN(s,&p,1); protocol("RX",base+string(1,p),c[id].nick+" solicita mover a la posicion "+string(1,p)+".");
            lock_guard<mutex> l(gameMtx); int k=c[id].room;
            if(k<0||c[id].role!='P'||c[id].symbol!=r[k].turn)continue;
            int pos=p-'1';
            if(pos<0||pos>8||r[k].b[pos]!='-'){ sendP(s,"TEPT","La posicion elegida esta ocupada."); continue; }
            r[k].b[pos]=r[k].turn; board(k);
            if(win(r[k])){
                int w=(r[k].turn=='X'?r[k].x:r[k].o),lo=(r[k].turn=='X'?r[k].o:r[k].x);
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
    while(1){ int s=accept(server,(sockaddr*)&a,&n),id=0; while(id<MAXC&&c[id].fd!=-1)id++; if(id==MAXC){close(s);continue;} c[id].fd=s; thread(clientTask,id).detach(); }
}
