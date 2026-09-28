// Halaman update firmware OTA (PROGMEM) — pola Program_CP_Logger web_server.h
#pragma once

#include "config.h"

const char ota_update_page_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MamaCare &#8212; Update Firmware</title>
<style>
:root{
  --orange:#F5A623;
  --orange-dark:#E09010;
  --orange-light:#FDE9C0;
  --navy:#1C2D6C;
  --white:#FFFFFF;
  --gray:#F7F8FC;
  --text:#2A2A2A;
  --muted:#8A8FA8
}
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  background:linear-gradient(150deg,var(--orange-light) 0%,var(--orange) 60%,var(--orange-dark) 100%);
  min-height:100vh;display:flex;align-items:center;
  justify-content:center;padding:20px
}
.card{
  background:var(--white);border-radius:24px;
  padding:32px 24px 24px;width:100%;max-width:400px;
  box-shadow:0 16px 48px rgba(28,45,108,.25)
}
.hdr{text-align:center;margin-bottom:20px}
.hdr h1{font-size:1.35rem;color:var(--navy);font-weight:800}
.hdr p{font-size:.82rem;color:var(--muted);margin-top:6px}
.warn{
  background:#FEF3C7;border-left:4px solid var(--orange);
  padding:12px 14px;border-radius:10px;margin-bottom:18px;
  font-size:.84rem;color:#92400E;line-height:1.5
}
.body-txt{color:var(--muted);font-size:.88rem;line-height:1.6;margin-bottom:14px}
.ssid-box{
  background:var(--gray);border:2px solid #E0E4EF;border-radius:12px;
  padding:14px 16px;margin:12px 0;font-size:.9rem
}
.ssid-box div{margin:6px 0;color:var(--navy)}
.ssid-box strong{display:inline-block;min-width:52px}
.btn-row{display:flex;flex-direction:column;gap:12px;margin-top:8px}
.btn{
  display:flex;align-items:center;justify-content:center;gap:8px;
  width:100%;padding:14px;border:none;border-radius:14px;
  font-size:.95rem;font-weight:700;cursor:pointer;text-decoration:none;
  box-shadow:0 4px 18px rgba(28,45,108,.25)
}
.btn-update{background:var(--navy);color:var(--white)}
.btn-back{background:var(--muted);color:var(--white)}
.btn:disabled{opacity:.55;cursor:not-allowed}
#status{display:none;margin-top:16px;padding:12px 14px;border-radius:10px;font-size:.85rem}
#status.show{display:block}
#status.ok{background:#D1FAE5;color:#065F46}
#status.err{background:#FEE2E2;color:#991B1B}
footer{text-align:center;font-size:.78rem;color:var(--muted);margin-top:20px}
</style>
</head>
<body>
<div class="card">
  <div class="hdr">
    <h1>Update Firmware</h1>
    <p>Versi )rawliteral" FIRMWARE_VERSION R"rawliteral(</p>
  </div>
  <div class="warn">
    Setelah menekan Update, perangkat memutus hotspot pairing dan menyambung ke hotspot berikut. Pastikan hotspot sudah aktif dan terhubung internet.
  </div>
  <p class="body-txt">Siapkan hotspot HP dengan SSID dan password berikut:</p>
  <div class="ssid-box">
    <div><strong>SSID:</strong> )rawliteral" OTA_STA_SSID R"rawliteral(</div>
    <div><strong>PASS:</strong> )rawliteral" OTA_STA_PASS R"rawliteral(</div>
  </div>
  <p class="body-txt" style="font-size:.8rem">Aktifkan hotspot di telepon, lalu tekan tombol di bawah dan jawab kedua pertanyaan konfirmasi.</p>
  <div class="btn-row">
    <button type="button" class="btn btn-update" id="btnOta">Update firmware</button>
    <a href="/" class="btn btn-back">Kembali ke Pengaturan WiFi</a>
  </div>
  <div id="status"></div>
  <footer>MamaCare OTA</footer>
</div>
<script>
function showStatus(msg,ok){
  var el=document.getElementById('status');
  el.textContent=msg;
  el.className='show '+(ok?'ok':'err');
}
document.getElementById('btnOta').addEventListener('click',function(){
  if(!confirm('1. Apakah Anda sudah mengonfirmasi ke admin?'))return;
  if(!confirm('2. Apakah hotspot sudah siap?'))return;
  var btn=document.getElementById('btnOta');
  btn.disabled=true;
  showStatus('Mengirim perintah update…',true);
  var xhttp=new XMLHttpRequest();
  xhttp.onreadystatechange=function(){
    if(this.readyState===4){
      if(this.status===200){
        showStatus('Perintah diterima. Perangkat ke mode STA — halaman ini akan terputus.',true);
      }else{
        showStatus('Gagal: HTTP '+this.status,false);
        btn.disabled=false;
      }
    }
  };
  xhttp.open('GET','/updateServer',true);
  xhttp.send();
});
</script>
</body>
</html>
)rawliteral";
