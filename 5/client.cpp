#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
using namespace std;

const int PORT=45000;
int s,room=-1; char role='-',symbol='-';
char b[9]={'-','-','-','-','-','-','-','-','-'};
bool running=true,myTurn=false,roomsReady=false;
mutex coutMtx,boardMtx,stateMtx; condition_variable cv;

string pad(int n,int k){ string x=to_string(n); return string(k-x.size(),'0')+x; }
void protocol(string t,string d,string msg){ lock_guard<mutex> l(coutMtx); cout<<"\n["<<t<<" PROTOCOL] "<<d<<"\n-> "<<msg<<"\n"; }
bool readN(char *x,int n){ int q,t=0; while(t<n&&(q=recv(s,x+t,n-t,0))>0)t+=q; return t==n; }
string readText(int n){ string x(n,' '); if(n)readN(&x[0],n); return x; }
void sendP(string m,string msg){ send(s,m.data(),m.size(),0); protocol("TX",m,msg); }
void table(){ lock_guard<mutex> a(boardMtx),o(coutMtx); cout<<"\n "<<b[0]<<" | "<<b[1]<<" | "<<b[2]<<"\n---+---+---\n "<<b[3]<<" | "<<b[4]<<" | "<<b[5]<<"\n---+---+---\n "<<b[6]<<" | "<<b[7]<<" | "<<b[8]<<"\n"; }

void receive(){
    while(1){
        char h; if(!readN(&h,1)){ lock_guard<mutex> l(stateMtx); running=false; cv.notify_all(); break; }
        if(h=='l'){
            char n[17]; readN(n,17); string x=readText(stoi(string(n,17)));
            protocol("RX","l"+string(n,17)+x,"Lista de jugadores/usuarios recibida."); cout<<"Jugadores conectados: "<<x<<"\n"; continue;
        }
        if(h!='T')continue;
        char op; readN(&op,1); string m="T"+string(1,op);

        if(op=='r'){
            char n[3]; readN(n,3); string x=readText(stoi(string(n,3)));
            protocol("RX",m+string(n,3)+x,"Lista de salas recibida."); cout<<"Salas disponibles: "<<x<<"\n";
            lock_guard<mutex> l(stateMtx); roomsReady=true; cv.notify_all();
        }
        else if(op=='S'){
            char n[7]; readN(n,7); string name=readText(stoi(string(n,7))); char x; readN(&x,1); symbol=x;
            protocol("RX",m+string(n,7)+name+x,"Estas en la sala '"+name+"' y juegas con "+string(1,x)+".");
        }
        else if(op=='Q'){
            char n[7]; readN(n,7); string name=readText(stoi(string(n,7)));
            protocol("RX",m+string(n,7)+name,"Esperando otro jugador en la sala '"+name+"'.");
        }
        else if(op=='G'){
            char n[7]; readN(n,7); string name=readText(stoi(string(n,7)));
            protocol("RX",m+string(n,7)+name,"La partida de la sala '"+name+"' comenzo.");
        }
        else if(op=='j'){
            char n[7]; readN(n,7); string name=readText(stoi(string(n,7)));
            protocol("RX",m+string(n,7)+name,"Entraste a la sala '"+name+"' como espectador.");
        }
        else if(op=='T'){
            char x[9]; readN(x,9); { lock_guard<mutex> l(boardMtx); for(int i=0;i<9;i++)b[i]=x[i]; }
            protocol("RX",m+string(x,9),"Tablero actualizado."); table();
        }
        else if(op=='t'){
            char x; readN(&x,1); m+=x; bool mine=(role=='P'&&symbol==x);
            { lock_guard<mutex> l(stateMtx); myTurn=mine; }
            protocol("RX",m,mine?"Es tu turno. Juegas con "+string(1,symbol)+".":"Turno del jugador "+string(1,x)+". Espera."); cv.notify_all();
        }
        else if(op=='E'){
            char x[2]; readN(x,2); m+=string(x,2); { lock_guard<mutex> l(stateMtx); myTurn=true; }
            protocol("RX",m,"Esa posicion esta ocupada. Elige otra."); cv.notify_all();
        }
        else if(op=='W'||op=='O'||op=='D'){
            string msg=op=='W'?"Ganaste la partida.":op=='O'?"Perdiste la partida.":"La partida termino en empate.";
            protocol("RX",m,msg); lock_guard<mutex> l(stateMtx); running=false; myTurn=false; cv.notify_all();
        }
    }
}

void requestRooms(){
    { lock_guard<mutex> l(stateMtx); roomsReady=false; }
    sendP("TR","Solicitando lista de salas.");
    unique_lock<mutex> l(stateMtx); cv.wait(l,[]{return roomsReady||!running;});
}

void joinSelected(){
    requestRooms(); string name; cout<<"Nombre de la sala (nombre del jugador que la creo): "; cin>>name;
    sendP("TJ"+pad(name.size(),7)+name,"Solicitando entrar a la sala '"+name+"'.");
}

void move(){
    int p; cout<<"Formato del protocolo: TM<posicion>  Ejemplo: TM5\nPosicion (1-9): "; cin>>p;
    if(p<1||p>9){ cout<<"Posicion invalida.\n"; return; }
    { lock_guard<mutex> l(boardMtx); if(b[p-1]!='-'){ cout<<"Esa posicion ya aparece ocupada.\n"; return; } }
    sendP("TM"+to_string(p),"Solicitando mover a la posicion "+to_string(p)+".");
    lock_guard<mutex> l(stateMtx); myTurn=false;
}

int main(int argc,char *argv[]){
    const char *ip=argc>1?argv[1]:"127.0.0.1"; s=socket(AF_INET,SOCK_STREAM,0); sockaddr_in a{};
    a.sin_family=AF_INET; a.sin_port=htons(PORT); inet_pton(AF_INET,ip,&a.sin_addr); connect(s,(sockaddr*)&a,sizeof(a));
