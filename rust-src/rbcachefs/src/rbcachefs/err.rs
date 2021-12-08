const MAX_ERROR_NO: isize = 4095;
// #define MAX_ERRNO	4095

// #define IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO)

fn is_error_value(val: isize) -> bool {
   val <= MAX_ERROR_NO
}

struct ErrorStruct {
   error_value: i32,


}
// impl Error for ErrorStruct 

#[tracing::instrument(err)]
pub fn ptr_result<'ptr, T>(ptr: *mut T ) -> anyhow::Result<&'ptr mut T> {
   match is_error_value(ptr as isize) {
      false => Ok(unsafe { &mut *ptr }),
      true => {
         // Documentation says strerror strings are supposed
         // To be 'static
         let c = unsafe {
            std::ffi::CStr::from_ptr( libc::strerror(-(ptr as i32)) )
         };
         Err(anyhow::anyhow!(c.to_string_lossy()))
      },
   }
}