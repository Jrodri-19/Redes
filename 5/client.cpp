#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
using namespace std;

const int PORT=45000;
int s,room=-1; char role='-',symbol='-';
char b[9]={'-','-','-','-','-','-','-','-','-'};
atomic<bool> running(true),myTurn(false);
mutex coutMtx,boardMtx;

string pad(int n,int k){ string x=to_string(n); return string(k-x.size(),'0')+x; }
void protocol(string t,string d,string msg){ lock_guard<mutex> l(coutMtx); cout<<"\n["<<t<<" PROTOCOL] "<<d<<"\n-> "<<msg<<"\n"; }
bool readN(char *x,int n){ int q,t=0; while(t<n&&(q=recv(s,x+t,n-t,0))>0)t+=q; return t==n; }
void sendP(string m,string msg){ send(s,m.data(),m.size(),0); protocol("TX",m,msg); }
void table(){ lock_guard<mutex> a(boardMtx),o(coutMtx); cout<<"\n "<<b[0]<<" | "<<b[1]<<" | "<<b[2]<<"\n---+---+---\n "<<b[3]<<" | "<<b[4]<<" | "<<b[5]<<"\n---+---+---\n "<<b[6]<<" | "<<b[7]<<" | "<<b[8]<<"\n"; }

void receive(){
    while(running){
        char h; if(!readN(&h,1)){ running=false; break; }

        if(h=='l'){
            char n[17]; readN(n,17); int k=stoi(string(n,17)); string x(k,' '); if(k)readN(&x[0],k);
            protocol("RX","l"+string(n,17)+x,"Lista de usuarios recibida."); cout<<"Usuarios: "<<x<<"\n"; continue;
        }
        if(h!='T') continue;
        char op; readN(&op,1); string m="T"+string(1,op);

        if(op=='r'){
            char n[3]; readN(n,3); int k=stoi(string(n,3)); string x(k,' '); if(k)readN(&x[0],k);
            protocol("RX",m+string(n,3)+x,"Salas disponibles recibidas."); cout<<"Salas: "<<x<<"\n";
        }
        else if(op=='S'){
            char x[3]; readN(x,3); room=stoi(string(x,2)); symbol=x[2];
            protocol("RX",m+string(x,3),"Estas en la sala "+to_string(room)+" y juegas con "+symbol+".");
        }
        else if(op=='Q'){
            char x[2]; readN(x,2); room=stoi(string(x,2));
            protocol("RX",m+string(x,2),"Esperando al segundo jugador en la sala "+to_string(room)+".");
        }
        else if(op=='G'){
            char x[2]; readN(x,2); protocol("RX",m+string(x,2),"La partida de la sala "+to_string(stoi(string(x,2)))+" comenzo.");
        }
        else if(op=='j'){
            char x[2]; readN(x,2); room=stoi(string(x,2));
            protocol("RX",m+string(x,2),"Entraste a la sala "+to_string(room)+" como espectador.");
        }
        else if(op=='T'){
            char x[9]; readN(x,9); { lock_guard<mutex> l(boardMtx); for(int i=0;i<9;i++)b[i]=x[i]; }
            protocol("RX",m+string(x,9),"Tablero actualizado."); table();
        }
        else if(op=='t'){
            char x; readN(&x,1); m+=x; myTurn=(role=='P'&&symbol==x);
            protocol("RX",m,myTurn?"Es tu turno. Juegas con "+string(1,symbol)+".":"Turno del jugador "+string(1,x)+". Espera.");
        }
        else if(op=='E'){
            char x[2]; readN(x,2); m+=string(x,2); myTurn=true;
            protocol("RX",m,"Esa posicion esta ocupada. Elige otra.");
        }
        else if(op=='W'){ protocol("RX",m,"Ganaste la partida."); myTurn=false; }
        else if(op=='O'){ protocol("RX",m,"Perdiste la partida."); myTurn=false; }
        else if(op=='D'){ protocol("RX",m,"La partida termino en empate."); myTurn=false; }
    }
}

void move(){
    if(!myTurn){ cout<<"Aun no es tu turno.\n"; return; }
    int p; cout<<"Formato: TM<posicion>  Ejemplo: TM5\nPosicion (1-9): "; cin>>p;
    if(p<1||p>9){ cout<<"Posicion invalida.\n"; return; }
    { lock_guard<mutex> l(boardMtx); if(b[p-1]!='-'){ cout<<"Esa posicion ya aparece ocupada.\n"; return; } }
    sendP("TM"+to_string(p),"Solicitando mover a la posicion "+to_string(p)+"."); myTurn=false;
}

int main(int argc,char *argv[]){
    const char *ip=argc>1?argv[1]:"127.0.0.1"; s=socket(AF_INET,SOCK_STREAM,0); sockaddr_in a{};
    a.sin_family=AF_INET; a.sin_port=htons(PORT); inet_pton(AF_INET,ip,&a.sin_addr); connect(s,(sockaddr*)&a,sizeof(a));
    cout<<"Conectado a "<<ip<<":"<<PORT<<"\n";

    string nick; cout<<"\nNickname: "; cin>>nick;
    sendP("N"+pad(nick.size(),7)+nick,"Enviando nickname: "+nick+".");

    int op; cout<<"\nMENU DE ROL\n1. JUGADOR    -> TP\n2. ESPECTADOR -> TV\nOpcion: "; cin>>op;
    role=op==1?'P':'V'; sendP(string("T")+role,role=='P'?"Te registraste como jugador.":"Te registraste como espectador.");

    thread rx(receive);

    while(running){
        if(role=='P'){
            cout<<"\nMENU JUGADOR\n1. Mover              -> TM<1-9>\n2. Mostrar tablero    -> local\n3. Listar usuarios    -> L\n4. Listar salas       -> TR\n5. Salir              -> Q\nOpcion: ";
            cin>>op;
            if(op==1) move();
            else if(op==2) table();
            else if(op==3) sendP("L","Solicitando lista de usuarios.");
            else if(op==4) sendP("TR","Solicitando lista de salas.");
            else if(op==5){ sendP("Q","Saliendo del servidor."); running=false; shutdown(s,SHUT_RDWR); }
        }else{
            cout<<"\nMENU ESPECTADOR\n1. Ver salas          -> TR\n2. Entrar a una sala  -> TJ<id>\n3. Mostrar tablero    -> local\n4. Listar usuarios    -> L\n5. Salir              -> Q\nOpcion: ";
            cin>>op;
            if(op==1) sendP("TR","Solicitando lista de salas disponibles.");
            else if(op==2){ int id; cout<<"Formato: TJ<id>  Ejemplo: TJ01\nID de sala: "; cin>>id; sendP("TJ"+pad(id,2),"Solicitando entrar a la sala "+to_string(id)+"."); }
            else if(op==3) table();
            else if(op==4) sendP("L","Solicitando lista de usuarios.");
            else if(op==5){ sendP("Q","Saliendo del servidor."); running=false; shutdown(s,SHUT_RDWR); }
        }
    }
    if(rx.joinable()) rx.join();
    close(s);
}
