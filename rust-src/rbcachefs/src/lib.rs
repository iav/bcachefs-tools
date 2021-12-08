pub mod c;
pub mod cmds;

type RResult<T> = std::io::Result<std::io::Result<T>>;

pub fn call_err_fn_for_c<F, FT>(c_fn: F) -> i32
where
	F: std::ops::FnOnce() -> anyhow::Result<FT>,
{
	init_tracing();

	match c_fn() {
		Ok(_) => 0, // Success
		Err(e) => {
			tracing::error!(fatal_error = ?e);
			1
		}
	}
}

pub fn init_tracing() {
	// convert existing log statements to tracing events
	// tracing_log::LogTracer::init().expect("logtracer init failed!");
	// format tracing log data to env_logger like stdout
	tracing_subscriber::fmt().with_env_filter(
		tracing_subscriber::EnvFilter::from_env("BCH_LOG")
	).init();
}
