# From Is there a way to identify the PCIe Speed for a device using powershell https://superuser.com/a/1732369/1731245
# Get all devices related to PCI BUS 
$pciStats = (Get-WMIObject Win32_Bus -Filter 'DeviceID like "PCI%"').GetRelated('Win32_PnPEntity') | 
  foreach {
    # request connection properties from wmi
    [pscustomobject][ordered]@{
      Name = $_.Name
      ExpressSpecVersion=$_.GetDeviceProperties('DEVPKEY_PciDevice_ExpressSpecVersion').deviceProperties.data
      MaxLinkSpeed      =$_.GetDeviceProperties('DEVPKEY_PciDevice_MaxLinkSpeed'      ).deviceProperties.data
      MaxLinkWidth      =$_.GetDeviceProperties('DEVPKEY_PciDevice_MaxLinkWidth'      ).deviceProperties.data
      CurrentLinkSpeed  =$_.GetDeviceProperties('DEVPKEY_PciDevice_CurrentLinkSpeed'  ).deviceProperties.data
      CurrentLinkWidth  =$_.GetDeviceProperties('DEVPKEY_PciDevice_CurrentLinkWidth'  ).deviceProperties.data
    } |
    # only keep devices with PCI connections
    Where MaxLinkSpeed
  }
$pciStats | Format-Table -AutoSize