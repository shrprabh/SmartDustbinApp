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
    for key, val in raw.items():
        ts_raw = val.get("timestamp")
        if isinstance(ts_raw, str):
            timestamp_dt = pd.to_datetime(ts_raw, utc=True)
        else:
            timestamp_dt = pd.to_datetime(ts_raw, unit="ms", utc=True)

        rows.append({
            "Temp (°C)":     val.get("temperature"),
            "Humidity (%)":  val.get("humidity"),
            "Status":        val.get("status"),
            "Timestamp":     timestamp_dt,   # for sorting & chart index
            "RawTimestamp":  ts_raw,         # original string for display
        })

    df = pd.DataFrame(rows)
    if not df.empty:
        df["Timestamp"] = pd.to_datetime(df["Timestamp"], utc=True)
        df = df.sort_values("Timestamp")
    return df

df = load_data()

st.title("🗑️ Smart Dustbin Real‑Time Dashboard")

if df.empty:
    st.warning("No data available yet.")
    st.stop()

# ——— Sidebar: status filter ———
st.sidebar.header("Status Filter")
all_statuses = df["Status"].unique().tolist()
selected = st.sidebar.multiselect(
    "Show only these statuses:",
    options=all_statuses,
    default=all_statuses
)
filtered_df = df[df["Status"].isin(selected)]

if filtered_df.empty:
    st.warning("No data for the selected status(es).")
    st.stop()

latest = filtered_df.iloc[-1]

# ——— Top‑row metrics ———
col1, col2, col3 = st.columns(3)
col1.metric("Temperature (°C)", f"{latest['Temp (°C)']}")
col2.metric("Humidity (%)", f"{latest['Humidity (%)']}")
col3.metric("Status", latest["Status"])

# ——— Historical trends ———
st.subheader("📈 Historical Trends")
trend_df = (
    filtered_df
    .set_index("Timestamp")[["Temp (°C)", "Humidity (%)"]]
)
st.line_chart(trend_df)

# ——— Complete log table ———
st.subheader("📋 All Logs")
table_df = (
    filtered_df
    .sort_values("Timestamp", ascending=False)
    [["RawTimestamp", "Temp (°C)", "Humidity (%)", "Status"]]
    .rename(columns={"RawTimestamp": "Timestamp"})
)
st.dataframe(table_df.reset_index(drop=True), height=400)
