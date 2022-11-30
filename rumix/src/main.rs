extern crate flatbuffers;

#[allow(dead_code, unused_imports)]
#[path = "JammerNetzAudioData_generated.rs"]
#[path = "JammerNetzPackages_generated.rs"]
mod JammerNetzAudioData_generated;
mod JammerNetzPackages_generated;
pub use JammerNetzAudioData_generated::{JammerNetzPNPAudioData, root_as_jammer_netz_pnpaudio_data };
pub use JammerNetzPackages_generated::JammerNetzPNPClientInfo;

use tokio::net::UdpSocket;
use std::io;

#[tokio::main]
async fn main() -> io::Result<()> {
    let sock = UdpSocket::bind("0.0.0.0:7778").await?;
    let mut buf = [0; 1024];
    loop {
        let (len, addr) = sock.recv_from(&mut buf).await?;
        println!("{:?} bytes received from {:?}", len, addr);
        // The first three bytes are a magic header and the message type
        if buf[0] as char == '1' && buf[1] as char == '2' && buf[2] as char == '3'
        {
            match buf[3]
            {
                1u8 => {
                    // This is an Audio packet, need to use Flatbuffers to deserialize it
                    let audio_packet = root_as_jammer_netz_pnpaudio_data(&buf[4..]);
                    if audio_packet.is_ok()
                    {
                        let audio_data = audio_packet.unwrap();
                    }
                    else {
                        print("Ignoring corrupted package that can not be decoded by Flatbuffers");
                    }
                }
                _ => println!("Ignoring unknown packet type with identfier {}", buf[3])
            }

        }

       // let len = sock.send_to(&buf[..len], addr).await?;
       // println!("{:?} bytes sent", len);
    }
}