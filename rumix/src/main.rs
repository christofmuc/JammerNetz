extern crate flatbuffers;
extern crate core;

#[allow(dead_code, unused_imports)]
#[path = "JammerNetzAudioData_generated.rs"]
mod JammerNetzAudioData_generated;

pub use JammerNetzAudioData_generated::{JammerNetzPNPAudioData, root_as_jammer_netz_pnpaudio_data };

use tokio::net::UdpSocket;
use std::io;
use std::net::SocketAddr;
use flume;
use std::sync::Arc;

struct MixerPackage {
    peer  : SocketAddr,
    audio : Vec<u8>
}

async fn accept_thread(sock: Arc<UdpSocket>, incoming_channel: flume::Sender<MixerPackage>) -> io::Result<()> {
    let mut buf = [0; 1024];
    let mut count = 0;
    loop {
        let (len, addr) = sock.recv_from(&mut buf).await?;
        if count % 128 == 0
        {
            println!("{:?} bytes received from {:?}", len, addr);
        }
        count += 1;
        println!("Received data from {:?} {:?}", addr, buf);
        // The first three bytes are a magic header and the message type
        if buf[0] as char == '1' && buf[1] as char == '2' && buf[2] as char == '3'
        {
            match buf[3]
            {
                1u8 => {
                    // This is an Audio packet, need to use Flatbuffers to deserialize it
                    let audio_message = Vec::from(&buf[4..]);
                    let audio_packet = root_as_jammer_netz_pnpaudio_data(&audio_message);
                    if audio_packet.is_ok()
                    {
                        let incoming = MixerPackage { peer: addr, audio: audio_message};
                        let posted_result = incoming_channel.send(incoming);
                    }
                    else {
                        println!("Ignoring corrupted package that can not be decoded by Flatbuffers");
                    }
                }
                _ => println!("Ignoring unknown packet type with identfier {}", buf[3])
            }

        }

       // let len = sock.send_to(&buf[..len], addr).await?;
       // println!("{:?} bytes sent", len);
    }
}

async fn mixer_thread(rx : flume::Receiver<MixerPackage>, send_queue: flume::Sender<MixerPackage>)
{
    loop {
        let packet = rx.recv_async().await;
        // better: time::timeout(TIMEOUT, self.incoming.recv_async()).await;
        if packet.is_ok()
        {
            let audio_data = packet.unwrap();
            let audio_packet = root_as_jammer_netz_pnpaudio_data(&audio_data.audio);
            if audio_packet.is_ok()
            {
                let jammer_data = audio_packet.unwrap();
                //println!("{:?}", jammer_data);
                send_queue.send(audio_data);
            } else {
                println!("Mixer thread got invalid audio packet, program error!");
            }
        }
        else
        {
            println!("Popped nothing from channel - why?");
        }
    }
}

async fn sender_thread(socket: Arc<UdpSocket>, send_queue: flume::Receiver<MixerPackage>)
{
    let mut buf = [0; 1024];
    buf[0] = '1' as u8;
    buf[1] = '2' as u8;
    buf[2] = '3' as u8;
    buf[3] = 1 as u8;
    loop {
        let outgoing = send_queue.recv_async().await;
        if outgoing.is_ok()
        {
            let message = outgoing.unwrap();
            for (dst, src) in buf[4..].iter_mut().zip(message.audio) {
                *dst = src;
            }
            println!("Sending data to {:?} {:?}", message.peer, buf);
            //socket.send_to(&buf, message.peer);
        }
    }
}

#[tokio::main]
async fn main()
{
    let sock = UdpSocket::bind("0.0.0.0:7778").await.unwrap();
    let sockarc = Arc::new(sock);
    let (tx, rx) = flume::unbounded::<MixerPackage>();
    let (sender_input, sender_queue) = flume::unbounded::<MixerPackage>();
    let accept_join = tokio::spawn(accept_thread(sockarc.clone(), tx));
    println!("Accept thread launched");
    let mixer_join = tokio::spawn(mixer_thread(rx, sender_input));
    println!("Mixer thread launched");
    let sender_join = tokio::spawn(sender_thread(sockarc, sender_queue));
    println!("Sender thread launched");
    let accept_result = accept_join.await;
    let mixer_result = mixer_join.await;
    let sender_result = sender_join.await;
    println!("Accept thread exit {:?}", accept_result);
    println!("Mixer thread exit {:?}", mixer_result);
    println!("Sender thread exit {:?}", sender_result);
}

