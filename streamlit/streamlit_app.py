import streamlit as st
import pandas as pd  # type: ignore
from pyrebase import pyrebase  # type: ignore
from streamlit_autorefresh import st_autorefresh  # type: ignore

# ——— Page config & auto-refresh ———
st.set_page_config(
    page_title="Smart Dustbin Dashboard",
    layout="wide",
    initial_sidebar_state="auto",
)
refresh_sec = st.sidebar.slider(
    "Refresh interval (seconds)", min_value=1, max_value=60, value=5
)
st_autorefresh(interval=refresh_sec * 1000, key="__refresh__")

# ——— Initialize Firebase ———
firebase_cfg = st.secrets["firebase"]
firebase = pyrebase.initialize_app(firebase_cfg)
db = firebase.database()

# ——— Data fetch & processing ———
def load_data():
    raw = db.child("trashLogs").get().val() or {}
    rows = []
    for _, val in raw.items():
        ts_raw = val.get("timestamp")
        if isinstance(ts_raw, str):
            timestamp_dt = pd.to_datetime(ts_raw, utc=True)
        else:
            timestamp_dt = pd.to_datetime(ts_raw, unit="ms", utc=True)
        rows.append({
            "Temp (°C)":    val.get("temperature"),
            "Humidity (%)": val.get("humidity"),
            "Status":       val.get("status"),
            "Timestamp":    timestamp_dt,
        })
    df = pd.DataFrame(rows)
    if not df.empty:
        df["Timestamp"] = pd.to_datetime(df["Timestamp"], utc=True)
        df = df.sort_values("Timestamp")
    return df

df = load_data()
if df.empty:
    st.title("🗑️ Smart Dustbin Real‑Time Dashboard")
    st.warning("No data available yet.")
    st.stop()

st.title("🗑️ Smart Dustbin Real‑Time Dashboard")

# ——— Sidebar: status filter ———
all_statuses = df["Status"].unique().tolist()
selected = st.sidebar.multiselect(
    "Show only these statuses:",
    options=all_statuses,
    default=all_statuses
)
filtered = df[df["Status"].isin(selected)]
if filtered.empty:
    st.warning("No data for the selected status(es).")
    st.stop()

# ——— Convert index to CDT for everything below ———
df_local = (
    filtered
    .set_index("Timestamp")
    .tz_convert("America/Chicago")
)

# ——— Top‑row current metrics ———
latest = df_local.iloc[-1]
c1, c2, c3 = st.columns(3)
c1.metric("Temperature (°C)", f"{latest['Temp (°C)']}")
c2.metric("Humidity (%)",    f"{latest['Humidity (%)']}")
c3.metric("Status",          latest["Status"])

# ——— Define time windows in local time ———
now      = pd.Timestamp.now(tz="America/Chicago")
day0     = now.normalize()
week0    = now - pd.Timedelta(days=7)
month0   = now - pd.Timedelta(days=30)
year0    = now - pd.Timedelta(days=365)

# ——— Split events ———
fill_df = df_local[df_local["Status"] == "TRASH_FULL"]
open_df = df_local[df_local["Status"] == "LID_OPENED"]

# ——— Compute counts ———
fill_counts = {
    "Today":     fill_df[fill_df.index >= day0].shape[0],
    "This Week": fill_df[fill_df.index >= week0].shape[0],
    "This Month":fill_df[fill_df.index >= month0].shape[0],
    "This Year": fill_df[fill_df.index >= year0].shape[0],
}
open_counts = {
    "Today":     open_df[open_df.index >= day0].shape[0],
    "This Week": open_df[open_df.index >= week0].shape[0],
    "This Month":open_df[open_df.index >= month0].shape[0],
    "This Year": open_df[open_df.index >= year0].shape[0],
}

# ——— Summary DataFrame for table & chart ———
stats_df = pd.DataFrame({
    "Fill Count":   fill_counts,
    "Opened Count": open_counts
}).rename_axis("Period").reset_index()

# ——— Display metrics table ———
st.subheader("📊 Fill & Open Summary")
st.table(stats_df)

# ——— Bar chart of counts ———
st.subheader("📊 Fill vs. Open by Period")
chart_df = stats_df.set_index("Period")[["Fill Count","Opened Count"]]
st.bar_chart(chart_df)

# ——— Historical trends ———
st.subheader("📈 Temperature & Humidity Trends")
trend = df_local[["Temp (°C)", "Humidity (%)"]]
st.line_chart(trend)

# ——— Complete log table ———
st.subheader("📋 All Logs")
logs = (
    df_local
    .sort_index(ascending=False)
    [["Temp (°C)", "Humidity (%)", "Status"]]
    .reset_index()
)
logs["Timestamp"] = logs["Timestamp"].dt.strftime("%Y-%m-%d %H:%M:%S")
st.dataframe(logs, height=400)
