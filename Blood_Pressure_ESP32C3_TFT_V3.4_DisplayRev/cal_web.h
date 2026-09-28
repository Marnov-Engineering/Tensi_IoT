// Halaman admin kalibrasi baterai — akses manual: http://<IP>/Cal (tidak ditautkan di UI)
#pragma once

const char cal_page_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kalibrasi Baterai</title>
<style>
:root{--navy:#1C2D6C;--orange:#F5A623;--gray:#F7F8FC;--muted:#8A8FA8;--ok:#065F46;--okbg:#D1FAE5}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--gray);min-height:100vh;padding:20px}
.card{max-width:420px;margin:0 auto;background:#fff;border-radius:16px;padding:24px;box-shadow:0 8px 32px rgba(28,45,108,.12)}
h1{font-size:1.2rem;color:var(--navy);margin-bottom:4px}
.sub{font-size:.82rem;color:var(--muted);margin-bottom:18px}
.live{background:var(--gray);border-radius:12px;padding:16px;margin-bottom:18px}
.live div{display:flex;justify-content:space-between;margin:8px 0;font-size:.95rem}
.live span{font-weight:700;color:var(--navy)}
label{display:block;font-size:.85rem;color:var(--muted);margin:12px 0 6px}
input{width:100%;padding:12px;border:2px solid #E0E4EF;border-radius:10px;font-size:.95rem}
.btn{width:100%;margin-top:18px;padding:14px;border:none;border-radius:12px;background:var(--navy);color:#fff;font-size:.95rem;font-weight:700;cursor:pointer}
.btn-off{background:#991B1B;margin-top:12px}
.msg{display:none;margin-bottom:14px;padding:12px;border-radius:10px;background:var(--okbg);color:var(--ok);font-size:.85rem}
.msg.show{display:block}
.lock-note{font-size:.78rem;color:var(--ok);margin-bottom:14px}
.hint{font-size:.78rem;color:var(--muted);margin-top:14px;line-height:1.5}
</style>
</head>
<body>
<div class="card">
  <h1>Kalibrasi Baterai</h1>
  <p class="sub">Admin — V = M &times; raw + C</p>
  <p class="lock-note">Alat tetap hidup selama countdown pairing (120 detik). Tutup halaman tidak memperpanjang waktu.</p>
  <div id="savedMsg" class="msg">Nilai M dan C tersimpan.</div>
  <div class="live">
    <div>ADC 12-bit <span id="rawVal">--</span></div>
    <div>Tegangan <span id="voltVal">-- V</span></div>
  </div>
  <form method="POST" action="/Cal/save">
    <label for="inpM">M (slope)</label>
    <input id="inpM" name="m" type="number" step="any" required>
    <label for="inpC">C (offset)</label>
    <input id="inpC" name="c" type="number" step="any" required>
    <button class="btn" type="submit">Simpan ke Preferences</button>
  </form>
  <form method="POST" action="/Cal/off" onsubmit="return confirm('Matikan alat?');">
    <button class="btn btn-off" type="submit">Matikan Alat</button>
  </form>
  <p class="hint">Raw dan tegangan diperbarui otomatis. Halaman ini tidak muncul di menu pairing.</p>
</div>
<script>
let formLoaded=false;
async function refresh(){
  try{
    const r=await fetch('/api/cal');
    const j=await r.json();
    document.getElementById('rawVal').textContent=j.raw;
    document.getElementById('voltVal').textContent=j.v.toFixed(3)+' V';
    if(!formLoaded){
      document.getElementById('inpM').value=j.m;
      document.getElementById('inpC').value=j.c;
      formLoaded=true;
    }
  }catch(e){}
}
if(location.search.indexOf('saved=1')>=0){
  document.getElementById('savedMsg').classList.add('show');
}
setInterval(refresh,500);
refresh();
</script>
</body>
</html>
)rawliteral";
