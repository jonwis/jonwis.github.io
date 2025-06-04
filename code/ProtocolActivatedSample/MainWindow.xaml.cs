using System.Diagnostics;
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
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;

namespace ProtocolActivatedSample
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Debugger.Launch();

            var args = AppInstance.GetActivatedEventArgs();
            if (args is ProtocolActivatedEventArgs protocolArgs)
            {
                LaunchingPackageNameTextBlock.Text = protocolArgs.CallerPackageFamilyName;
                ProtocolUri.Text = protocolArgs.Uri.ToString();
            }
        }
    }
}