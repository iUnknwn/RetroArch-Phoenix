#ifdef _WIN32

#include <nall/http.hpp>
#include <nall/thread.hpp>
#include <nall/file.hpp>
#include <nall/zip.hpp>
#include "updater.hpp"

using namespace nall;

Updater::Updater()
{
   setTitle("SSNES || Update");
   onClose = [this]{ this->hide(); };

   timer.onTimeout = {&Updater::timer_event, this};
   timer.setInterval(50);
   timer.setEnabled(false);

#if 0
   server_label.setText("URL:");
   server_hbox.append(server_label, 100, 0, 10);
   server_url.setText(base_url);"http://themaister.net/ssnes-dl/latest");
   server_hbox.append(server_url, 300, 0);

   vbox.append(server_hbox);
#endif
   version_download.setText("Check version");
   dl_layout.append(version_download, 0, 0);
   download.setText("Download");
   dl_layout.append(download, 0, 0);
   cancel_download.setText("Cancel");
   dl_layout.append(cancel_download, 0, 0);
   vbox.append(dl_layout, 5);

   progress.setPosition(0);
   progress_layout.append(progress, 300, 0, 10);
   progress_label.setText("N/A");
   progress_layout.append(progress_label, 100, 0, 3);
   vbox.append(progress_layout);

   opts_32bit.setText("x86");
   opts_64bit.setText("x64");
   opts_slim.setText("Slim");
   opts_full.setText("Full");
   opts_redist.setText("Redist");
   RadioBox::group(opts_32bit, opts_64bit);
   RadioBox::group(opts_slim, opts_full, opts_redist);

   opts_arch.setText("CPU:");
   opts_layout.append(opts_arch, 0, 0, 5);
   opts_layout.append(opts_32bit, 0, 0);
   opts_layout.append(opts_64bit, 0, 0, 30);
   opts_build.setText("Build:");
   opts_layout.append(opts_build, 0, 0, 5);
   opts_layout.append(opts_slim, 0, 0);
   opts_layout.append(opts_full, 0, 0);
   opts_layout.append(opts_redist, 0, 0);
   vbox.append(opts_layout, 3);

   opts_32bit.setChecked();
   opts_slim.setChecked();

   latest_label.setText("Latest release: N/A");
   vbox.append(latest_label, ~0, 0, 20);

   libsnes_label.setText("Cores:");
   vbox.append(libsnes_label, 0, 0);

   libsnes_listview.setHeaderText("System", "Core", "Version", "Architecture", "Library");
   libsnes_listview.setHeaderVisible();
   libsnes_listview.autoSizeColumns();
   libsnes_listview.setEnabled(false);
   vbox.append(libsnes_listview, ~0, 250);
   libsnes_dlhint.setText("Double-click core to download. Use core by setting libsnes path to desired library.");
   vbox.append(libsnes_dlhint, 0, 0);

   vbox.setMargin(5);

   auto minimum = vbox.minimumGeometry();
   setGeometry({128, 128, minimum.width, minimum.height});


   version_download.onTick = [this] {
      transfer.version_only = true;
      start_download(latest_file());
   };

   download.onTick = [this] {
      transfer.version_only = false;
      
      nall::string path;
      path.append(opts_redist.checked() ? "SSNES-win" : "ssnes-win");
      path.append(opts_32bit.checked() ? "32-" : "64-");
      if (opts_redist.checked())
         path.append("libs.zip");
      else
      {
         path.append(transfer.version, "-");
         path.append(opts_slim.checked() ? "slim.zip" : "full.zip");
      }

      start_download(path);
   };

   libsnes_listview.onActivate = [this] {
      transfer.version_only = false;

      const auto &elem = libsnes_list[libsnes_listview.selection()];
      start_download({elem.basename, ".zip"});
   };

   cancel_download.onTick = [this] {
      nall::scoped_lock lock(transfer.lock);
      transfer.cancelled = true;
   };

   append(vbox);

   disable_downloads();
   cancel_download.setEnabled(false);
}

void Updater::start_download(const string &path)
{
   transfer.finished = false;
   transfer.success = false;
   transfer.cancelled = false;
   transfer.now = 0;
   transfer.total = 0;
   transfer.data.clear();

   progress.setPosition(0);
   disable_downloads();
   cancel_download.setEnabled(true);

   transfer.file_path = path;
   spawn_thread(&Updater::download_thread, this, path);

   timer.setEnabled(true);
}

void Updater::cancel()
{
   hide();
   progress_label.setText("N/A");
   nall::scoped_lock lock(transfer.lock);
   transfer.cancelled = true;
}

void Updater::show()
{
   setVisible();
}

void Updater::hide()
{
   setVisible(false);
}

bool Updater::extract_zip(const nall::string &path)
{
   
   nall::zip z;
   if (!z.open({basedir(), path}))
   {
      MessageWindow::critical(*this, "Failed opening ZIP!");
      return false;
   }

   foreach (file, z.file)
   {
      // Don't overwrite config files.
      if (file.name.endswith(".cfg") && nall::file::exists({basedir(), file.name}))
         continue;

      uint8_t *data;
      unsigned size;
      if (!z.extract(file, data, size))
         continue;

      if (!nall::file::write({basedir(), file.name}, data, size))
      {
         delete [] data;
         continue;
      }

      delete [] data;
   }

   return true;
}

Updater::libsnes_desc Updater::line2desc(const nall::string &line)
{
   nall::lstring list;
   list.split(",", line);

   static const nall::string na = "<Invalid>";
   if (list.size() < 5)
      return {na, na, na, na, na};

   return { // This is starting to look like JavaScript ... :D
      list[0].trim(" ", " "),
      list[1].trim(" ", " "),
      list[2].trim(" ", " "),
      list[3].trim(" ", " "),
      list[4].trim(" ", " "),
   };
}

void Updater::end_transfer_list()
{
   if (transfer.data.size() == 0)
      return;

   transfer.data.push_back('\0');

   nall::lstring list;
   list.split("\n", transfer.data.data());
   if (list.size() == 0)
      return;

   transfer.version = list[0];
   list.remove(0);

   string latest("Latest release: ", transfer.version);
   latest_label.setText({"Latest release: ", transfer.version});

   version_download.setEnabled(false);

   foreach (elem_, list)
   {
      if (elem_.length() > 0)
      {
         const auto &elem = line2desc(elem_);
         libsnes_list.append(elem);
         libsnes_listview.append(elem.system, elem.core, elem.version, elem.arch, elem.basename);
      }
   }

   libsnes_listview.autoSizeColumns();
}

void Updater::end_file_transfer()
{
   bool valid = false;
   if (nall::file::write({basedir(), transfer.file_path},
            (const uint8_t *)transfer.data.data(),
            transfer.data.size()))
   {
      valid = true;
   }
   else
      MessageWindow::critical(*this, "Failed saving archive to disk!");

   if (valid && extract_zip(transfer.file_path))
      MessageWindow::information(*this, "Extracted archive! :)");
   else if (valid)
      MessageWindow::critical(*this, "Failed opening ZIP!");
}

void Updater::timer_event()
{
   nall::scoped_lock lock(transfer.lock);

   if (transfer.finished)
   {
      timer.setEnabled(false);
      if (transfer.success)
      {
         progress.setPosition(100);

         if (transfer.version_only)
            end_transfer_list();
         else
            end_file_transfer();
      }
      else
         MessageWindow::warning(*this, "Download was not completed!");

      cancel_download.setEnabled(false);

      if (transfer.version.length() > 0)
         enable_downloads();
   }
   else if (transfer.cancelled)
   {
      timer.setEnabled(false);
      enable_downloads();
      cancel_download.setEnabled(false);
   }

   update_progress();
}

void Updater::update_progress()
{
   string text;
   if (transfer.now > 1000000)
   {
      text.append(nall::fp(transfer.now / 1000000.0, 3));
      text.append(" MB");
   }
   else if (transfer.now > 1000)
   {
      text.append(nall::fp(transfer.now / 1000.0, 3));
      text.append(" kB");
   }
   else
      text.append(transfer.now, " B");

   if (transfer.total)
      progress.setPosition(transfer.now * 100 / transfer.total);

   progress_label.setText(text);
}

void Updater::download_thread(const nall::string &path)
{
   nall::http dl;
   dl.write_cb = {&Updater::update, this};
   dl.progress_cb = {&Updater::progress_update, this};

   bool ret;
   if ((ret = dl.connect(base_host())))
   {
      ret = dl.download({base_folder(), path});
      dl.disconnect();
   }

   nall::scoped_lock lock(transfer.lock);
   transfer.success = ret;
   transfer.finished = true;
}

void Updater::update(const char *content, unsigned size)
{
   nall::scoped_lock lock(transfer.lock);
   transfer.data.insert(transfer.data.end(), content, content + size);
   transfer.now = transfer.data.size();
}

bool Updater::progress_update(unsigned now, unsigned total)
{
   nall::scoped_lock lock(transfer.lock);

   transfer.now = now;
   transfer.total = total;

   return !transfer.cancelled;
}

nall::string Updater::basedir()
{
   // Windows is completely batshit retarded and the relative path might "change" by going into the file manager, so we have to manually figure out the full path. :)
#ifdef _WIN32
   char dir_path[MAX_PATH];
   GetModuleFileName(GetModuleHandle(0), dir_path, sizeof(dir_path));
   char *split = strrchr(dir_path, '\\');
   if (!split) split = strrchr(dir_path, '/');
   if (split) split[1] = '\0';
   return dir_path;
   //MessageWindow::information(*this, path);
#else
   return nall::string();
#endif
}

void Updater::enable_downloads()
{
   download.setEnabled(true);
   libsnes_listview.setEnabled(true);
}

void Updater::disable_downloads()
{
   download.setEnabled(false);
   libsnes_listview.setEnabled(false);
}


#endif

