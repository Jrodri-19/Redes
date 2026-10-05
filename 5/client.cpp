
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
    cout<<"Conectado a "<<ip<<":"<<PORT<<"\n";

    string nick; cout<<"\nNickname: "; cin>>nick;
    sendP("N"+pad(nick.size(),7)+nick,"Enviando nickname: "+nick+".");
    thread rx(receive);

    int op;
    while(role=='-'){
        cout<<"\nMENU INICIAL\n"
            <<"1. Crear una sala          -> TP + TC\n"
            <<"2. Unirse a una sala       -> TP + TJ<nombre>\n"
            <<"3. Ver sala como espectador-> TV + TJ<nombre>\n"
            <<"4. Listar jugadores        -> L\n"
            <<"5. Listar salas            -> TR\n"
            <<"6. Salir                    -> Q\nOpcion: ";
        cin>>op;
        if(op==1){ role='P'; sendP("TP","Te registraste como jugador."); sendP("TC","Creando una sala con tu nickname: "+nick+"."); }
        else if(op==2){ role='P'; sendP("TP","Te registraste como jugador."); joinSelected(); }
        else if(op==3){ role='V'; sendP("TV","Te registraste como espectador."); joinSelected(); }
        else if(op==4) sendP("L","Solicitando lista de jugadores.");
        else if(op==5) requestRooms();
        else if(op==6){ sendP("Q","Saliendo del servidor."); { lock_guard<mutex> l(stateMtx); running=false; } shutdown(s,SHUT_RDWR); }
        if(!running)break;
    }

    while(true){
        { lock_guard<mutex> l(stateMtx); if(!running)break; }

        if(role=='P'){
            unique_lock<mutex> l(stateMtx); cv.wait(l,[]{return myTurn||!running;}); if(!running)break; l.unlock();
            bool done=false;
            while(!done){
                cout<<"\nMENU DE TU TURNO\n"
                    <<"1. Mover              -> TM<1-9>\n"
                    <<"2. Mostrar tablero    -> local\n"
                    <<"3. Listar jugadores   -> L\n"
                    <<"4. Listar salas       -> TR\n"
                    <<"5. Salir               -> Q\nOpcion: ";
                cin>>op;
                if(op==1){ move(); done=true; }
                else if(op==2) table();
                else if(op==3) sendP("L","Solicitando lista de jugadores.");
                else if(op==4) requestRooms();
                else if(op==5){ sendP("Q","Saliendo del servidor."); { lock_guard<mutex> x(stateMtx); running=false; } shutdown(s,SHUT_RDWR); done=true; }
            }
        }else{
            cout<<"\nMENU ESPECTADOR\n"
                <<"1. Ver salas           -> TR\n"
                <<"2. Cambiar de sala     -> TJ<nombre>\n"
                <<"3. Mostrar tablero     -> local\n"
                <<"4. Listar jugadores    -> L\n"
                <<"5. Salir                -> Q\nOpcion: ";
            cin>>op;
            if(op==1) requestRooms();
            else if(op==2) joinSelected();
            else if(op==3) table();
            else if(op==4) sendP("L","Solicitando lista de jugadores.");
            else if(op==5){ sendP("Q","Saliendo del servidor."); { lock_guard<mutex> l(stateMtx); running=false; } shutdown(s,SHUT_RDWR); }
        }
    }

    if(rx.joinable()) rx.join();
    close(s);
}
