import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.esp32c6
@idf_parametrize("target", ["esp32c6"], indirect=["target"])
def test_nerve_os_boot(dut: IdfDut) -> None:
    dut.expect("NERVE OS listo: swipe CORE/HAND/SENSE, doble tap watchface", timeout=15)
    dut.expect("OK: NERVE OS + touch + auto-rotacion segura + WiFi/NTP", timeout=15)
