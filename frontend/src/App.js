import { useEffect, useState, useCallback } from "react";
import "@/App.css";
import { BrowserRouter, Routes, Route } from "react-router-dom";
import axios from "axios";
import { QRCodeCanvas } from "qrcode.react";
import {
  Download,
  Copy,
  Check,
  ScanLine,
  ShieldAlert,
  Wifi,
  CheckCircle2,
  AlertTriangle,
  FileArchive,
} from "lucide-react";

const BACKEND_URL = process.env.REACT_APP_BACKEND_URL;
const API = `${BACKEND_URL}/api`;

function formatBytes(bytes) {
  if (!bytes && bytes !== 0) return "—";
  const units = ["o", "Ko", "Mo", "Go"];
  let i = 0;
  let n = bytes;
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024;
    i++;
  }
  return `${n.toFixed(i === 0 ? 0 : 2)} ${units[i]}`;
}

function CopyButton({ text, label, testId }) {
  const [copied, setCopied] = useState(false);
  const onCopy = useCallback(async () => {
    try {
      await navigator.clipboard.writeText(text);
    } catch (e) {
      const ta = document.createElement("textarea");
      ta.value = text;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      document.body.removeChild(ta);
    }
    setCopied(true);
    setTimeout(() => setCopied(false), 1600);
  }, [text]);

  return (
    <button className="copy-btn" onClick={onCopy} data-testid={testId} type="button">
      {copied ? <Check size={15} /> : <Copy size={15} />}
      <span>{copied ? "Copié" : label}</span>
    </button>
  );
}

const Home = () => {
  const [info, setInfo] = useState(null);
  const [error, setError] = useState(null);

  const downloadUrl = info ? `${BACKEND_URL}${info.download_path}` : "";

  const fetchInfo = useCallback(async () => {
    try {
      const res = await axios.get(`${API}/installer/info`);
      setInfo(res.data);
    } catch (e) {
      setError(
        "Le fichier .cia n'est pas encore disponible sur le serveur. Lancez d'abord ./build.sh."
      );
      console.error(e);
    }
  }, []);

  useEffect(() => {
    fetchInfo();
  }, [fetchInfo]);

  return (
    <div className="page">
      <div className="glow glow-a" />
      <div className="glow glow-b" />

      <header className="top">
        <div className="brand">
          <div className="brand-badge brand-badge-logo">
            <img src="/tricord-icon.png" alt="TriCord Presence" width={40} height={40} />
          </div>
          <div>
            <h1 className="brand-title">TriCord Presence</h1>
            <p className="brand-sub">Rich Presence Discord pour Nintendo 3DS &middot; Install FBI par QR</p>
          </div>
        </div>
        <span className="pill" data-testid="version-pill">
          {info ? `v${info.version}` : "…"}
        </span>
      </header>

      <main className="grid">
        <section className="card card-qr" data-testid="qr-card">
          <div className="card-head">
            <ScanLine size={18} />
            <h2>Scanner avec FBI</h2>
          </div>

          {error ? (
            <div className="error-box" data-testid="error-box">
              <AlertTriangle size={18} />
              <span>{error}</span>
            </div>
          ) : (
            <>
              <div className="qr-frame" data-testid="qr-frame">
                {downloadUrl ? (
                  <QRCodeCanvas
                    value={downloadUrl}
                    size={248}
                    level="M"
                    marginSize={2}
                    bgColor="#ffffff"
                    fgColor="#0b0c14"
                  />
                ) : (
                  <div className="qr-skeleton" />
                )}
              </div>

              <p className="qr-caption">
                Ouvre <b>FBI</b> &rarr; <b>Remote Install</b> &rarr;{" "}
                <b>Scan QR Code</b>, puis vise ce code.
              </p>

              <div className="url-row">
                <code className="url" data-testid="download-url" title={downloadUrl}>
                  {downloadUrl || "…"}
                </code>
                {downloadUrl && (
                  <CopyButton text={downloadUrl} label="Copier l'URL" testId="copy-url-btn" />
                )}
              </div>

              {downloadUrl && (
                <a
                  className="dl-btn"
                  href={downloadUrl}
                  data-testid="manual-download-btn"
                >
                  <Download size={17} />
                  Télécharger le .cia manuellement
                </a>
              )}
            </>
          )}
        </section>

        <div className="col">
          <section className="card" data-testid="steps-card">
            <div className="card-head">
              <ScanLine size={18} />
              <h2>Étapes</h2>
            </div>
            <ol className="steps">
              <li>
                <span className="step-n">1</span>
                <div>
                  Ta 3DS et ce site doivent avoir accès à Internet (FBI télécharge
                  le fichier en Wi-Fi).
                </div>
              </li>
              <li>
                <span className="step-n">2</span>
                <div>
                  Lance <b>FBI</b> &rarr; <b>Remote Install</b> &rarr;{" "}
                  <b>Scan QR Code</b>.
                </div>
              </li>
              <li>
                <span className="step-n">3</span>
                <div>Vise le QR code ci-contre. FBI télécharge puis installe le .cia.</div>
              </li>
              <li>
                <span className="step-n">4</span>
                <div>
                  Lance l'app installée <b>TriCord Presence Installer</b> depuis le
                  menu HOME pour copier le sysmodule sur la SD.
                </div>
              </li>
            </ol>
          </section>

          <section className="card" data-testid="fileinfo-card">
            <div className="card-head">
              <FileArchive size={18} />
              <h2>Fichier</h2>
            </div>
            <div className="kv">
              <span>Nom</span>
              <code data-testid="info-filename">{info?.filename || "—"}</code>
            </div>
            <div className="kv">
              <span>Version</span>
              <code>{info ? `v${info.version}` : "—"}</code>
            </div>
            <div className="kv">
              <span>Taille</span>
              <code data-testid="info-size">{formatBytes(info?.size)}</code>
            </div>
            <div className="kv kv-hash">
              <span>SHA-256</span>
              <div className="hash-wrap">
                <code data-testid="info-sha256">{info?.sha256 || "—"}</code>
                {info?.sha256 && (
                  <CopyButton text={info.sha256} label="Copier" testId="copy-sha-btn" />
                )}
              </div>
            </div>
          </section>
        </div>
      </main>

      <section className="card notes" data-testid="notes-card">
        <div className="card-head">
          <ShieldAlert size={18} />
          <h2>Avant d'installer</h2>
        </div>
        <ul className="notes-list">
          <li>
            <CheckCircle2 size={16} className="ok" />
            <span>
              <b>TriCord</b> doit être installé et connecté à un compte Discord :
              le sysmodule réutilise ce compte, aucun token à saisir.
            </span>
          </li>
          <li>
            <Wifi size={16} className="ok" />
            <span>
              Dans Luma3DS : active <b>« Enable loading external FIRMs and modules »</b>{" "}
              (config Luma, SELECT au boot).
            </span>
          </li>
          <li>
            <ShieldAlert size={16} className="warn" />
            <span>
              Connexion Gateway avec un token utilisateur = usage « self-bot » au
              sens des CGU Discord. Teste avec un <b>compte jetable</b>, pas ton
              compte principal.
            </span>
          </li>
          <li>
            <AlertTriangle size={16} className="warn" />
            <span>
              FBI doit gérer le HTTPS pour ce téléchargement. Si le scan échoue,
              utilise le bouton de téléchargement manuel puis installe le .cia
              depuis la SD.
            </span>
          </li>
        </ul>
      </section>

      <footer className="foot">
        TriCord Presence · Rich Presence Discord pour Nintendo 3DS (Luma3DS)
      </footer>
    </div>
  );
};

function App() {
  return (
    <div className="App">
      <BrowserRouter>
        <Routes>
          <Route path="/" element={<Home />} />
        </Routes>
      </BrowserRouter>
    </div>
  );
}

export default App;
