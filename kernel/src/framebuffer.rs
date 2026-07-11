//! Pure framebuffer geometry and pixel encoding helpers.

use finn_boot_protocol::PixelFormat;

/// Return the byte offset of a 32-bit pixel after checking geometry and size.
#[must_use]
pub fn pixel_offset(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    stride: u32,
    byte_len: u64,
) -> Option<usize> {
    if x >= width || y >= height || stride < width {
        return None;
    }
    let offset = (u64::from(y)
        .checked_mul(u64::from(stride))?
        .checked_add(u64::from(x))?)
    .checked_mul(4)?;
    let end = offset.checked_add(4)?;
    if end > byte_len {
        None
    } else {
        usize::try_from(offset).ok()
    }
}

/// Encode a 24-bit RGB color into a 32-bit framebuffer pixel.
#[must_use]
pub fn encode_pixel(format: PixelFormat, red: u8, green: u8, blue: u8) -> Option<u32> {
    match format {
        PixelFormat::Rgb => {
            Some(u32::from(red) | (u32::from(green) << 8) | (u32::from(blue) << 16))
        }
        PixelFormat::Bgr => {
            Some(u32::from(blue) | (u32::from(green) << 8) | (u32::from(red) << 16))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn offset_handles_stride() {
        assert_eq!(pixel_offset(1, 1, 2, 2, 4, 32), Some(20));
    }
    #[test]
    fn rejects_out_of_range_and_short_buffers() {
        assert_eq!(pixel_offset(2, 0, 2, 1, 2, 8), None);
        assert_eq!(pixel_offset(1, 0, 2, 1, 2, 7), None);
    }
    #[test]
    fn encodes_rgb_and_bgr() {
        assert_eq!(encode_pixel(PixelFormat::Rgb, 1, 2, 3), Some(0x0003_0201));
        assert_eq!(encode_pixel(PixelFormat::Bgr, 1, 2, 3), Some(0x0001_0203));
    }
}
