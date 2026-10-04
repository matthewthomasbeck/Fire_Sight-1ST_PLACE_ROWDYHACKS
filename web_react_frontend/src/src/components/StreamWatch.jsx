import StreamPlayer from "./StreamPlayer";
import LidarPanel from "./LidarPanel";
import GpsBar from "./GpsBar";
import { gpsIdFromStreamUrl, lidarIdFromStreamUrl } from "../resourceIds";
import "./StreamWatch.css";

const LIDAR_PX = 480;
const VIDEO_W = Math.round((LIDAR_PX * 3) / 4);

// Portrait MJPEG + square lidar side by side, GPS coords under both.
export default function StreamWatch({ url }) {
  const lidarId = lidarIdFromStreamUrl(url);
  const gpsId = gpsIdFromStreamUrl(url);

  return (
    <section className="watch">
      <div
        className="watch__panels"
        style={{
          display: "flex",
          flexWrap: "wrap",
          justifyContent: "center",
          alignItems: "flex-start",
          gap: 16,
        }}
      >
        <div className="watch__video" style={{ width: VIDEO_W, flex: "0 0 auto" }}>
          <StreamPlayer url={url} frameWidth={VIDEO_W} frameHeight={LIDAR_PX} />
        </div>
        <LidarPanel lidarId={lidarId} size={LIDAR_PX} />
      </div>
      <GpsBar gpsId={gpsId} />
    </section>
  );
}
