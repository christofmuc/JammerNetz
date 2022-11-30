extern crate flatbuffers;

#[allow(dead_code, unused_imports)]
#[path = "JammerNetzAudioData_generated.rs"]
mod JammerNetzAudioData_generated;

pub use JammerNetzAudioData_generated::{JammerNetzPNPAudioData, root_as_jammer_netz_pnpaudio_data };

use tokio::net::UdpSocket;
use std::io;
use flume;

async fn accept_thread(incoming_channel: flume::Sender<Vec<u8>>) -> io::Result<()> {
    let sock = UdpSocket::bind("0.0.0.0:7778").await?;
    let mut buf = [0; 1024];
    let mut count = 0;
    loop {
        let (len, addr) = sock.recv_from(&mut buf).await?;
        if count % 128 == 0
        {
            println!("{:?} bytes received from {:?}", len, addr);
        }
        count += 1;
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
                        let posted_result = incoming_channel.send(audio_message);
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

async fn mixer_thread(rx : flume::Receiver<Vec<u8>>)
{
    loop {
        let packet = rx.recv_async().await;
        // better: time::timeout(TIMEOUT, self.incoming.recv_async()).await;
        if packet.is_ok()
        {
            let audio_data = packet.unwrap();
            let audio_packet = root_as_jammer_netz_pnpaudio_data(&audio_data);
            if audio_packet.is_ok()
            {
                let audio_data = audio_packet.unwrap();
                println!("{:?}", audio_data);
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

#[tokio::main]
async fn main()
{
    let (tx, rx) = flume::unbounded::<Vec::<u8>>();
    let accept_join = tokio::spawn(accept_thread(tx));
    println!("Accept thread launched");
    let mixer_join = tokio::spawn(mixer_thread(rx));
    println!("Mixer thread launched");
    let accept_result = accept_join.await;
    let mixer_result = mixer_join.await;
    println!("Accept thread exit {:?}", accept_result);
}

