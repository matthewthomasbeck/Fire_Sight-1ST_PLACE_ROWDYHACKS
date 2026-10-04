import StreamPlayer from "./StreamPlayer";
import LidarPanel from "./LidarPanel";
import GpsBar from "./GpsBar";
import { gpsIdFromStreamUrl, lidarIdFromStreamUrl } from "../resourceIds";
import "./StreamWatch.css";

// Portrait MJPEG + square lidar side by side, GPS coords under both.
export default function StreamWatch({ url }) {
  const lidarId = lidarIdFromStreamUrl(url);
  const gpsId = gpsIdFromStreamUrl(url);

  return (
    <section className="watch">
      <div className="watch__panels">
        <div className="watch__video">
          <StreamPlayer url={url} />
        </div>
        <LidarPanel lidarId={lidarId} />
      </div>
      <GpsBar gpsId={gpsId} />
    </section>
  );
}
