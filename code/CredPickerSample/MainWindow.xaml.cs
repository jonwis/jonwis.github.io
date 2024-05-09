using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Windows.Security.Credentials;
using Windows.Security.Credentials.UI;

namespace CredPickerSample
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        private async void Clickery_Click(object sender, RoutedEventArgs e)
        {
            // Show a credential picker to acquire the user's domain credentials.
            var credPicker = new CredentialPickerOptions
            {
                Message = "Sign in",
                Caption = "Windows Credential Picker",
                TargetName = "Windows Authentication",
                AuthenticationProtocol = AuthenticationProtocol.Negotiate,
                CredentialSaveOption = CredentialSaveOption.Hidden,
                CallerSavesCredential = false,
            };

            var credResult = await CredentialPicker.PickAsync(credPicker);
            Clickery.Content = credResult.ErrorCode == 0 ? "Success" : "Failure";
            if (credResult.Credential != null)
            {
                // Convert the IBuffer in credResult.Credential to a base64 string
                var credentialBuffer = Windows.Security.Cryptography.CryptographicBuffer.EncodeToBase64String(credResult.Credential);
                Clickery.Content += $"\n{credentialBuffer}";
            }
            else
            {
                Clickery.Content += $"\n{credResult.ErrorCode}";
            }

        }
    }
}